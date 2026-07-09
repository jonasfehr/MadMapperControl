#include "ofApp.h"
#include "CueGridBuilder.h"
#include "OscQueryJson.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <sstream>

using namespace oscq;

static bool midiPortMatches(const std::vector<std::string>& ports, const std::string& name);

namespace {
	std::string resolveCustomPagesPath() {
		const std::string dataPath = ofToDataPath("custom_page.json", true);
		if (ofFile::doesFileExist(dataPath, false)) return dataPath;

		const std::string cwdPath = ofFilePath::join(ofFilePath::getCurrentWorkingDirectory(), "bin/data/custom_page.json");
		if (ofFile::doesFileExist(cwdPath, false)) return cwdPath;

		return dataPath;
	}

	std::string resolveSettingsPath() {
		const std::string dataPath = ofToDataPath("settings.json", true);
		if (ofFile::doesFileExist(dataPath, false)) return dataPath;

		const std::string cwdPath = ofFilePath::join(ofFilePath::getCurrentWorkingDirectory(), "bin/data/settings.json");
		if (ofFile::doesFileExist(cwdPath, false)) return cwdPath;

		return dataPath;
	}

	ofJson ensurePagesShape(const ofJson& data) {
		ofJson shaped = ofJson::object();
		shaped["pages"] = (data.is_object() && data.contains("pages") && data["pages"].is_array())
			? data["pages"]
			: ofJson::array();
		shaped["subpages"] = (data.is_object() && data.contains("subpages") && data["subpages"].is_array())
			? data["subpages"]
			: ofJson::array();
		return shaped;
	}

	ofJson sanitizeServersArray(const ofJson& servers) {
		ofJson sanitized = ofJson::array();
		if (!servers.is_array()) return sanitized;

		std::unordered_set<std::string> seenHostPort;
		size_t index = 0;
		for (const auto& item : servers) {
			if (!item.is_object()) continue;
			auto ipIt = item.find("ip");
			const std::string ip = (ipIt != item.end() && ipIt->is_string()) ? ipIt->get<std::string>() : std::string();
			auto qpIt = item.find("queryPort");
			const int query = (qpIt != item.end() && qpIt->is_number()) ? qpIt->get<int>() : PORT_RECEIVE;
			if (ip.empty() || query <= 0 || query > 65535) continue;

			const std::string dedupeKey = ofToLower(ip) + ":" + ofToString(query);
			if (seenHostPort.count(dedupeKey)) continue;
			seenHostPort.insert(dedupeKey);

			auto idIt = item.find("id");
			auto spIt = item.find("sendPort");
			auto fpIt = item.find("feedbackPort");
			auto discIt = item.find("discovery");

			ofJson out = ofJson::object();
			out["id"] = (idIt != item.end() && idIt->is_string()) ? idIt->get<std::string>() : "server_" + ofToString(index);
			out["ip"] = ip;
			out["queryPort"] = query;
			out["sendPort"] = (spIt != item.end() && spIt->is_number()) ? spIt->get<int>() : query;
			out["feedbackPort"] = (fpIt != item.end() && fpIt->is_number()) ? fpIt->get<int>() : PORT_FEEDBACK;
			out["discovery"] = (discIt != item.end() && discIt->is_string()) ? discIt->get<std::string>() : std::string("manual");
			sanitized.push_back(out);
			++index;
		}

		if (sanitized.empty()) {
			ofJson fallback = ofJson::object();
			fallback["id"] = "MadMapper";
			fallback["ip"] = "127.0.0.1";
			fallback["queryPort"] = PORT_RECEIVE;
			fallback["sendPort"] = PORT_RECEIVE;
			fallback["feedbackPort"] = PORT_FEEDBACK;
			fallback["discovery"] = "manual";
			sanitized.push_back(fallback);
		}

		return sanitized;
	}


	std::string labelForRoleOrPrefix(ofxMidiDevice* dev, const std::string& role, const std::string& fallbackPrefix, int index) {
		auto bit = dev->bindings.find(role);
		if (bit != dev->bindings.end() && dev->midiComponents.count(bit->second)) return bit->second;
		std::string fallback = fallbackPrefix + ofToString(index);
		if (dev->midiComponents.count(fallback)) return fallback;
		return std::string();
	}

	std::string normalizePageKey(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
			if (c == ' ') return '_';
			return static_cast<char>(std::tolower(c));
		});
		return value;
	}

	// Surface subpage name = path segment before "opacity" in the parameter's OSC address.
	std::string surfaceSubpageName(MadParameter* parameter) {
		if (!parameter || !parameter->isSelectable()) return std::string();
		auto oscAddress = ofSplitString(parameter->getOscAddress(), "/");
		int i = 0;
		while (i < (int)oscAddress.size() && oscAddress[i] != "opacity") i++;
		if (i <= 0 || i >= (int)oscAddress.size()) return std::string();
		return oscAddress[i - 1];
	}

	bool pageContainsOscPrefix(MadParameterPage* page, const std::string& prefix) {
		if (!page) return false;
		auto* params = page->getParameters();
		if (!params || params->empty()) return false;
		for (auto* p : *params) {
			if (!p) continue;
			if (p->getOscAddress().rfind(prefix, 0) == 0) return true;
		}
		return false;
	}

	MadParameterPage* findSubPageByNameAndPrefix(std::list<MadParameterPage>& subPages,
											  const std::string& candidate,
											  const std::string& preferredPrefix) {
		MadParameterPage* exactFallback = nullptr;
		MadParameterPage* normalizedFallback = nullptr;
		if (candidate.empty()) return nullptr;

		for (auto& page : subPages) {
			if (page.getName() == candidate) {
				if (pageContainsOscPrefix(&page, preferredPrefix)) return &page;
				if (!exactFallback) exactFallback = &page;
			}
		}

		const std::string wanted = normalizePageKey(candidate);
		for (auto& page : subPages) {
			if (normalizePageKey(page.getName()) == wanted) {
				if (pageContainsOscPrefix(&page, preferredPrefix)) return &page;
				if (!normalizedFallback) normalizedFallback = &page;
			}
		}

		return exactFallback ? exactFallback : normalizedFallback;
	}

	MadParameter* visibleParameterAt(MadParameterPage* page, int buttonIndex) {
		if (!page || buttonIndex <= 0) return nullptr;
		auto* params = page->getParameters();
		if (!params || params->empty()) return nullptr;
		auto range = page->getRange();
		int absoluteOneBased = range.first + (buttonIndex - 1);
		if (absoluteOneBased <= 0 || static_cast<size_t>(absoluteOneBased) > params->size()) return nullptr;
		auto it = params->begin();
		std::advance(it, absoluteOneBased - 1);
		return *it;
	}

	bool isComponentMappedToRole(ofxMidiDevice* dev, const std::string& componentName, const std::string& roleSuffix) {
		if (!dev || componentName.empty()) return false;
		for (int i = 1; i <= 16; ++i) {
			const std::string role = "param." + ofToString(i) + roleSuffix;
			auto bit = dev->bindings.find(role);
			if (bit != dev->bindings.end() && bit->second == componentName) return true;
		}
		return false;
	}

}

static MidiComponent* getComponentByRole(ofxMidiDevice* dev, const std::string& role) {
	if (!dev) return nullptr;
	auto it = dev->bindings.find(role);
	if (it == dev->bindings.end()) return nullptr;
	auto mc = dev->midiComponents.find(it->second);
	if (mc == dev->midiComponents.end()) return nullptr;
	return &mc->second;
}

//--------------------------------------------------------------
void ofApp::setup() {
	ofSetFrameRate(60);
	settings = ofLoadJson("settings.json");
	oscServers.setConfigs(OscServerConfig::parseList(settings));
	oscServers.onPrimaryBecameReachable = [this]() {
		if (!reconnectInProgress.load()) hasPendingReconnect.store(true);
	};
	ip = oscServers.configs().front().ip;
	sendPort = oscServers.configs().front().sendPort;
	queryPort = oscServers.configs().front().queryPort;
	feedbackPort = oscServers.configs().front().feedbackPort;
	if (settings.contains("cueBankName") && settings["cueBankName"].is_string()) {
		cueBankName = settings["cueBankName"].get<std::string>();
	}
	if (settings.contains("cueFollowActiveBank") && settings["cueFollowActiveBank"].is_boolean()) {
		cueFollowActiveBank = settings["cueFollowActiveBank"].get<bool>();
	}
	// settings.json can still override the hover encoder OSC path (legacy support)
	{
		auto pathIt = settings.find("tdHoverEncoderPath");
		if (pathIt != settings.end() && pathIt->is_string())
			tdHoverEncoderOscPath = pathIt->get<std::string>();
	}
	// Identify which server is "TouchDesigner" for hover encoder routing (default).
	// loadMappings() will override these from mappings.json if present.
	tdServerId = SIZE_MAX;
	for (size_t i = 0; i < oscServers.configs().size(); ++i) {
		if (oscServers.configs()[i].id == "TouchDesigner") {
			tdServerId = i;
			break;
		}
	}

	loadMappings();

	// Which profile the web emulator drives when no hardware is present.
	emulatorProfileName = settings.value("emulatorProfile", std::string("Push3"));

	// Select first matching connected device (same path as hot-plug reconnect)
	noDeviceConnected = true;
	tryConnectMidiDevice();
	if (noDeviceConnected) {
		ofLogWarning() << "No MIDI device/profile matched. Running without controller.";
	}

	oscServers.setupPrimary();
	currentPage = madOscQuery.pages.end();
	previousPage = madOscQuery.pages.end();
	{
		std::lock_guard<std::mutex> lock(activePageMutex);
		activePageName.clear();
	}
	oscServers.refreshEndpointHealth(true);

	if (oscServers.reachable(0)) {
		madOscQuery.receive();
		ofSleepMillis(100);
	}
	if (!oscServers.reachable(0) || madOscQuery.madMapperJson == nullptr) {
		ofLog(OF_LOG_WARNING) << "Load unsuccessful!";
	} else {
		float p = 1;
		auto success = reloadFromServer(p);
		madMapperLoadError = !success;
		if (!madMapperLoadError) {
			bool wsConnected = madOscQuery.connectWebSocket(queryPort);
			if (!wsConnected) {
				ofLogWarning() << "OSCQuery WebSocket connection failed on port " << queryPort;
			} else {
				madOscQuery.subscribeAllParameters();
				oscServers.registerPathRouting(0, madOscQuery);
				subscribeTimelinePaths();
				ofLogNotice() << "OSCQuery WebSocket connected on port " << queryPort;
			}
			oscServers.setupAdditionalServers();

			// Pages were initially built before additional servers existed.
			// Rebuild once so serverId>0 pages are available at startup.
			// Mark initialised=true first so reloadFromServer clears groups before re-adding.
			initialised = true;
			float rebuildAllPages = 1.f;
			reloadFromServer(rebuildAllPages);
		} else {
			initialised = true;
		}
	}
	errorImage.load("debug.png");

	// Setup web server for REST API
	setupWebServer();
}

// CALBACK FUNCTIONS
// --------------------------------------------------------
void ofApp::selectSubpageFromButton(const std::string& buttonName,
                                    const std::string& skipRoleSuffix,
                                    const std::string& oscPrefix,
                                    const std::function<std::string(MadParameter*)>& subpageNameFor,
                                    const std::function<void(const std::string&)>& oscFallback) {
	if (currentPage == madOscQuery.pages.end()) return;
	if (surface) {
		auto* dev = static_cast<ofxMidiDevice*>(surface.get());
		if (isComponentMappedToRole(dev, buttonName, skipRoleSuffix)) return;
		auto mit = dev->midiComponents.find(buttonName);
		if (mit != dev->midiComponents.end() && mit->second.value.get() < 0.5f) return;
	}

	// If already on a subpage and button is pressed, go back to main page
	if ((*currentPage).isSubpage()) {
		float p = 1.f;
		backToCurrent(p);
		return;
	}

	auto tokens = ofSplitString(buttonName, "_");
	if (tokens.empty()) return;
	MadParameter* parameter = visibleParameterAt(&(*currentPage), ofToInt(tokens.back()));
	if (!parameter || !parameter->isSelectable()) return;

	const std::string subpageName = subpageNameFor(parameter);
	if (subpageName.empty()) return;

	previousPage = currentPage;
	if (auto* target = findSubPageByNameAndPrefix(madOscQuery.subPages, subpageName, oscPrefix)) {
		MadParameterPage* prevPage = &(*currentPage);
		for (auto pageIt = madOscQuery.subPages.begin(); pageIt != madOscQuery.subPages.end(); ++pageIt) {
			if (&(*pageIt) == target) {
				currentPage = pageIt;
				setActivePage(&(*currentPage), prevPage);
				return;
			}
		}
	}
	oscFallback(subpageName);
}

void ofApp::selectSurface(string& name) {
	selectSubpageFromButton(name, ".mediaSubpage", "/surfaces/",
		[](MadParameter* p) { return surfaceSubpageName(p); },
		[this](const std::string& n) { oscSelectSurface(n); });
}

void ofApp::selectMedia(string& name) {
	selectSubpageFromButton(name, ".layerSubpage", "/media/",
		[](MadParameter* p) { return p->getConnectedMediaName(); },
		[this](const std::string& n) { oscSelectMedia(n); });
}

void ofApp::backToCurrent(float& p) {
	MadParameterPage* prevPage = &(*currentPage);
	currentPage = previousPage;
	setActivePage(&(*currentPage), prevPage);

	for (auto& midiComponent : selectGroup.midiComponents) {
		midiComponent.second->value.disableEvents();
		midiComponent.second->value = 0;
		midiComponent.second->update();
		midiComponent.second->value.enableEvents();
	}
}

// ======= CHANNEL CONTROL =======
void ofApp::pageForward(float& p) {
	if (p == 1) {
		(*currentPage).cycleForward();
	}
	updateParameterDisplay();
}

void ofApp::pageBackward(float& p) {
	if (p == 1) {
		(*currentPage).cycleBackward();
	}
	updateParameterDisplay();
}

// ======= BANK CONTROL =======
void ofApp::bankForward(float& p) {
	if (next(currentPage) != madOscQuery.pages.end() && p == 1) {
		MadParameterPage* prevPage = &(*currentPage);
		currentPage++;
		setActivePage(&(*currentPage), prevPage);
	}
}

void ofApp::bankBackward(float& p) {
	if (currentPage != madOscQuery.pages.begin() && p == 1) {
		MadParameterPage* prevPage = &(*currentPage);
		currentPage--;
		setActivePage(&(*currentPage), prevPage);
	}
}

void ofApp::updateValues(float& p) {
	if (p == 1 && !isLoading) {
		isLoading = true;
		madOscQuery.updateValues();
		rebuildCueGrid(madOscQuery.madMapperJson);
		updateParameterDisplay(); // refresh display with updated values
	} else
		isLoading = false;
}

void ofApp::removeListeners() {
	if (!surface) return;
	auto* dev = static_cast<ofxMidiDevice*>(surface.get());
	unbindCueGrid();
	ofRemoveListener(madOscQuery.webSocketPathE, this, &ofApp::onWebSocketPathUpdate);
	if (currentPage != madOscQuery.pages.end()) currentPage->unlinkDevice();
	if (auto c = ::getComponentByRole(dev, "nav.pageNext")) c->value.removeListener(this, &ofApp::pageForward);
	if (auto c = ::getComponentByRole(dev, "nav.pagePrev")) c->value.removeListener(this, &ofApp::pageBackward);
	if (auto c = ::getComponentByRole(dev, "nav.bankNext")) c->value.removeListener(this, &ofApp::bankForward);
	if (auto c = ::getComponentByRole(dev, "nav.bankPrev")) c->value.removeListener(this, &ofApp::bankBackward);
	if (auto c = ::getComponentByRole(dev, "nav.reload")) c->value.removeListener(this, &ofApp::updateValues);
	if (auto c = ::getComponentByRole(dev, "action.back")) c->value.removeListener(this, &ofApp::backToCurrent);

	// Remove page shortcut listeners
	for (auto& listener : pageGotoListeners) {
		if (auto* c = ::getComponentByRole(dev, listener.role))
			c->value.removeListener(&listener, &PageGotoListener::onPress);
	}
	pageGotoListeners.clear();

	if (fadeMasterVideo && dev->midiComponents.count("fader_M_video"))
		fadeMasterVideo->unlinkMidiComponent(dev->midiComponents["fader_M_video"]);
	if (fadeMasterDMX && dev->midiComponents.count("fader_M_dmx"))
		fadeMasterDMX->unlinkMidiComponent(dev->midiComponents["fader_M_dmx"]);
	if (fadeEngineSpeed && dev->midiComponents.count("fader_Speed"))
		fadeEngineSpeed->unlinkMidiComponent(dev->midiComponents["fader_Speed"]);
	if (speed && dev->midiComponents.count("jog"))
		speed->unlinkMidiComponent(dev->midiComponents["jog"]);
	fadeMasterVideo = nullptr;
	fadeMasterDMX   = nullptr;
	fadeEngineSpeed  = nullptr;
	speed            = nullptr;

	for (auto& lp : linkedFixedParams) lp.param->unlinkMidiComponent(*lp.component);
	linkedFixedParams.clear();
	activeFixedBindings.clear();

	ofRemoveListener(selectGroup.lastChangedE, this, &ofApp::selectSurface);
	ofRemoveListener(muteGroup.lastChangedE, this, &ofApp::selectSurface);
	ofRemoveListener(muteGroup.noneSelectedE, this, &ofApp::backToCurrent);
	ofRemoveListener(soloGroup.lastChangedE, this, &ofApp::selectMedia);
}

// OSC FUNCTIONS
// -------------------------------------------------------------
void ofApp::oscSelectSurface(string name) {
	oscSelectSurface(name, 0);
}

void ofApp::oscSelectSurface(string name, size_t serverId) {
	string oscAddress = "/surfaces/" + name + "/select";

	ofxOscMessage m;
	m.setAddress(oscAddress);
	m.addFloatArg(1);
	oscServers.sendTo(serverId, m);
}

void ofApp::oscSelectMedia(string name) {
	oscSelectMedia(name, 0);
}

void ofApp::oscSelectMedia(string name, size_t serverId) {
	string oscAddress = "/media/select_by_name";

	ofxOscMessage m;
	m.setAddress(oscAddress);
	m.addStringArg(name);
	oscServers.sendTo(serverId, m);
}

//--------------------------------------------------------------
void ofApp::update() {
	applyPendingServerConfig();
	oscServers.pollHealthAsync();
	oscServers.refreshBonjourServices();

	if (hasPendingReconnect.exchange(false) && !reconnectInProgress.load()) {
		reconnectInProgress.store(true);
		std::thread([this]() {
			ofLogNotice("ofApp") << "Primary endpoint became reachable — auto-reconnecting OSCQuery...";
			{
				std::lock_guard<std::mutex> lock(oscServers.stateMutex());
				if (oscServers.connectPrimary()) {
					subscribeTimelinePaths();
				}
			}
			hasPendingReload.store(true);
			reconnectInProgress.store(false);
		}).detach();
	}

	if (hasPendingReload.exchange(false)) {
		reloadRequested = true;
	}

	if (hasPendingBindingsUpdate.exchange(false)) {
		applyBindingsUpdate();
	}

	// Web emulator requested a different virtual surface — swap on the main thread.
	// Real hardware always wins; we only re-point the surface the emulator owns.
	if (hasPendingEmulatorSwitch.exchange(false)) {
		std::string wanted;
		{
			std::lock_guard<std::mutex> lock(emulatorMutex);
			wanted = pendingEmulatorProfile;
		}
		if (!wanted.empty() && virtualSurface && wanted != emulatorProfileName) {
			emulatorProfileName = wanted;
			disconnectMidiDevice();
			tryConnectMidiDevice();
			ofLogNotice("ofApp") << "Emulator surface switched to " << emulatorProfileName;
		}
	}

	// Deliver any web-injected MIDI on the main thread (after a possible swap).
	drainInjectedMidi();

	// MIDI hot-plug: scan for port changes every 2 seconds
	{
		const uint64_t nowMidi = ofGetElapsedTimeMillis();
		if (initialised && (nowMidi - lastMidiScanMs >= 2000)) {
			lastMidiScanMs = nowMidi;
			const auto currentInPorts = ofxMidiIn().getInPortList();
			if (currentInPorts != lastKnownInPorts) {
				lastKnownInPorts = currentInPorts;
				if (noDeviceConnected || virtualSurface) {
					tryConnectMidiDevice(); // upgrades a virtual surface when hardware appears
				} else if (activeProfile) {
					bool stillPresent = midiPortMatches(currentInPorts, activeProfile->midiInPort);
					if (!stillPresent) {
						disconnectMidiDevice();
						tryConnectMidiDevice(); // fall back to the virtual surface
					}
				}
			}
		}
	}

	const uint64_t nowMs = ofGetElapsedTimeMillis();
	if (reloadRequested && (nowMs - lastReloadMs >= 3000)) {
		float reloadButton = 1.f;
		reloadFromServer(reloadButton);
		lastReloadMs = nowMs;
		reloadRequested = false;
	}

	std::string pageToActivate;
	{
		std::lock_guard<std::mutex> lock(pendingPageMutex);
		if (hasPendingPageActivation) {
			pageToActivate = pendingPageName;
			hasPendingPageActivation = false;
		}
	}
	if (!pageToActivate.empty()) {
		activatePageByName(pageToActivate);
	}

	if (cueGridRefreshPending && !isLoading && !madMapperLoadError) {
		const uint64_t now = ofGetElapsedTimeMillis();
		if (now - lastCueGridRefreshMs >= 120) {
			madOscQuery.receive();
			rebuildCueGrid(madOscQuery.madMapperJson);
			cueGridRefreshPending = false;
			lastCueGridRefreshMs = now;
		}
	}

	// Delta-mode fixed bindings: poll, accelerate, send raw OSC delta.
	// Absolute-mode bindings are listener-driven via MadParameter::linkMidiComponent
	// (acceleration lives in MadParameter::onParameterChange). Tune kFixedAccelBase/Max
	// and kHoverAccelBase/Max in ofApp.h; tune MadParameter::encoderAccelBase/Max in MadParameter.h.
	for (auto& fb : activeFixedBindings) {
		const float v     = fb.component->value.get();
		const float delta = v - fb.prevValue;
		fb.prevValue = v;
		if (delta == 0.f) continue;

		const uint64_t now = ofGetElapsedTimeMillis();
		const uint64_t dt  = now - fb.lastMs;
		fb.lastMs = now;

		float accel = 1.f;
		if (dt > 0 && dt < 500) {
			const float velocity = 1000.f / static_cast<float>(dt);
			accel = std::min(velocity / fb.accelBase, fb.accelMax);
			if (accel < 1.f) accel = 1.f;
		}

		const float scaledDelta = std::round(delta * accel * kEncoderSensitivity * 1000.f) / 1000.f;
		if (scaledDelta == 0.f) continue;

		ofxOscMessage m;
		m.setAddress(fb.mapping.path);
		m.addFloatArg(scaledDelta);
		oscServers.sendTo(fb.mapping.serverId, m);
	}

	// Throttled refresh so displays (e.g. Push3) show value changes without saturating USB/CPU.
	static uint64_t lastDisplayRefreshMs = 0;
	const uint64_t nowMsDisplay = ofGetElapsedTimeMillis();
	if (!isLoading && initialised && !madMapperLoadError && surface && currentPage != madOscQuery.pages.end()) {
		if (nowMsDisplay - lastDisplayRefreshMs >= 32) {
			updateParameterDisplay();
			updateSubpageMediaButtonFeedback();
			lastDisplayRefreshMs = nowMsDisplay;
		}
	}
}

void ofApp::setupPages(ofJson madmapperJson) {
	currentPage = madOscQuery.pages.begin();
	previousPage = currentPage;

	setActivePage(&(*currentPage), nullptr);
}

void ofApp::setupUI(ofJson madmapperJson) {
	if (!surface) return;
	auto* dev = static_cast<ofxMidiDevice*>(surface.get());
	rebuildCueGrid(madmapperJson);
	bindCueGrid();

	if (auto c = ::getComponentByRole(dev, "nav.pageNext")) c->value.addListener(this, &ofApp::pageForward);
	if (auto c = ::getComponentByRole(dev, "nav.pagePrev")) c->value.addListener(this, &ofApp::pageBackward);
	if (auto c = ::getComponentByRole(dev, "nav.bankNext")) c->value.addListener(this, &ofApp::bankForward);
	if (auto c = ::getComponentByRole(dev, "nav.bankPrev")) c->value.addListener(this, &ofApp::bankBackward);
	if (auto c = ::getComponentByRole(dev, "nav.reload")) c->value.addListener(this, &ofApp::updateValues);
	if (auto c = ::getComponentByRole(dev, "action.back")) c->value.addListener(this, &ofApp::backToCurrent);

	// Wire page shortcut buttons (nav.pageGoto.N → activatePageByName)
	pageGotoListeners.clear();
	for (auto& entry : pageGotoEntries) {
		if (entry.pageName.empty()) continue;
		auto* c = ::getComponentByRole(dev, entry.role);
		if (!c) continue;
		pageGotoListeners.push_back({ entry.role, entry.pageName, this });
		c->value.addListener(&pageGotoListeners.back(), &PageGotoListener::onPress);
	}

	const ofJson* mappingOpacity =
		jsonGet(madmapperJson, {"CONTENTS", "surfaces", "CONTENTS", "Mapping", "CONTENTS", "opacity"});
	const ofJson* masterVideo = mappingOpacity
									? mappingOpacity
									: jsonGet(madmapperJson, {"CONTENTS", "master", "CONTENTS", "master_video_level"});
	if (masterVideo && dev->midiComponents.count("fader_M_video")) {
		fadeMasterVideo = madOscQuery.createParameter(*masterVideo);
		fadeMasterVideo->linkMidiComponent(dev->midiComponents["fader_M_video"]);
	}
	const ofJson* lightingOpacity =
		jsonGet(madmapperJson, {"CONTENTS", "surfaces", "CONTENTS", "Lighting", "CONTENTS", "opacity"});
	const ofJson* masterDMX = lightingOpacity
								  ? lightingOpacity
								  : jsonGet(madmapperJson, {"CONTENTS", "master", "CONTENTS", "master_dmx_level"});
	if (masterDMX && dev->midiComponents.count("fader_M_dmx")) {
		fadeMasterDMX = madOscQuery.createParameter(*masterDMX);
		fadeMasterDMX->linkMidiComponent(dev->midiComponents["fader_M_dmx"]);
	}
	const ofJson* engineSpeed = jsonGet(madmapperJson, {"CONTENTS", "master", "CONTENTS", "engine_speed"});
	if (engineSpeed && dev->midiComponents.count("fader_Speed")) {
		fadeEngineSpeed = madOscQuery.createParameter(*engineSpeed);
		fadeEngineSpeed->linkMidiComponent(dev->midiComponents["fader_Speed"]);
	}
	const ofJson* bpm = jsonGet(madmapperJson, {"CONTENTS", "master", "CONTENTS", "Global_BPM", "CONTENTS", "BPM"});
	if (bpm && dev->midiComponents.count("jog")) {
		speed = madOscQuery.createParameter(*bpm);
		speed->linkMidiComponent(dev->midiComponents["jog"]);
	}

	// Generic fixed OSC mappings: wire all fixed.* bindings.
	// For absolute mode, create a MadParameter to get min/max range + MM feedback sync.
	activeFixedBindings.clear();
	linkedFixedParams.clear();
	for (auto& [key, fm] : fixedMappings) {
		auto* c = ::getComponentByRole(dev, "fixed." + key);
		if (!c) continue;

		if (fm.mode == "absolute") {
			ofxMadOscQuery* query = oscServers.server(fm.serverId);
			if (query && query->madMapperJson.is_object()) {
				if (const ofJson* node = resolveNodeByPath(query->madMapperJson, fm.path)) {
					MadParameter* param = query->createParameter(*node);
					if (param) {
						param->linkMidiComponent(*c); // acceleration + feedback sync built-in
						linkedFixedParams.push_back({ c, param });
						ofLogNotice("ofApp") << "Fixed binding linked: fixed." << key << " → "
						                     << fm.path << " [" << param->range.min
						                     << ".." << param->range.max << "]";
					}
				}
			}
		} else {
			// Delta mode: poll in update() and send raw delta OSC.
			// The TD hover encoder gets faster acceleration than regular encoders.
			const bool hover = (key == "tdHoverEncoder");
			activeFixedBindings.push_back({ c, fm, c->value.get(), 0,
			                                hover ? kHoverAccelBase : kFixedAccelBase,
			                                hover ? kHoverAccelMax  : kFixedAccelMax });
			ofLogNotice("ofApp") << "Fixed binding wired: fixed." << key << " → " << fm.path << " (delta)";
		}
	}

	selectGroup.doCheckbox = true;
	for (int i = 1; i < 17; i++) {
		std::string lbl = labelForRoleOrPrefix(dev, "param." + ofToString(i) + ".layerSubpage", "sel_", i);
		if (dev->midiComponents.count(lbl)) selectGroup.add(dev->midiComponents[lbl]);
	}
	ofAddListener(selectGroup.lastChangedE, this, &ofApp::selectSurface);

	muteGroup.doCheckbox = false;
	for (int i = 1; i < 17; i++) {
		std::string lbl = labelForRoleOrPrefix(dev, "param." + ofToString(i) + ".groupSubpage", "mute_", i);
		if (dev->midiComponents.count(lbl)) muteGroup.add(dev->midiComponents[lbl]);
	}
	ofAddListener(muteGroup.lastChangedE, this, &ofApp::selectSurface);
	ofAddListener(muteGroup.noneSelectedE, this, &ofApp::backToCurrent);

	soloGroup.doCheckbox = true;
	for (int i = 1; i < 17; i++) {
		std::string lbl = labelForRoleOrPrefix(dev, "param." + ofToString(i) + ".mediaSubpage", "solo_", i);
		if (dev->midiComponents.count(lbl)) soloGroup.add(dev->midiComponents[lbl]);
	}
	ofAddListener(soloGroup.lastChangedE, this, &ofApp::selectMedia);
}

//--------------------------------------------------------------
void ofApp::draw() {
	if (!isLoading) {
		if (!madMapperLoadError) {
			ofBackground(0);
			if (showStatusString) drawStatusString();
			if (noDeviceConnected) {
				ofDrawBitmapStringHighlight("No MIDI controller connected", 15, 30);
			} else if (virtualSurface) {
				ofDrawBitmapStringHighlight("VIRTUAL surface — control via web emulator", 15, 30);
			}
		} else {
			std::string s = "MADMAPPER HTTP ENDPOINT NOT FOUND - TRY AGAIN!";
			ofDrawBitmapStringHighlight(s, 15, 15);
			if (errorImage.isAllocated()) {
				ofPushMatrix();
				ofTranslate(ofGetWidth() / 2, ofGetHeight() / 2);
				errorImage.draw(-errorImage.getWidth() / 2, -errorImage.getHeight() / 2,
								errorImage.getWidth(), errorImage.getHeight());
				ofPopMatrix();
			}
		}
		if (showMidiIn && surface) {
			auto* dev = static_cast<ofxMidiDevice*>(surface.get());
			dev->gui.setPosition(10, 10);
			dev->gui.draw();
		}
	}

	stringstream windowInfo;
	windowInfo << "| MMCntrl | FPS: " << std::fixed << std::setprecision(1) << ofGetFrameRate();
	windowInfo << " | " << ip << " (" << sendPort << "/" << feedbackPort << "/" << queryPort << ") |";
	if (!cueBankName.empty()) {
		windowInfo << " CUE " << cueBankName;
		if (cueFollowActiveBank) windowInfo << "*";
	}
	if (noDeviceConnected) windowInfo << " NO MIDI";
	if (virtualSurface) windowInfo << " VIRTUAL";

	ofSetWindowTitle(windowInfo.str());
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
	float p = 1; // for use in callback functions

	if (key == 's') {
		showStatusString = !showStatusString;
	}
	if (key == 'm') {
		showMidiIn = !showMidiIn;
	}

	if (key == ' ') {
		madOscQuery.updateValues();
		rebuildCueGrid(madOscQuery.madMapperJson);
		updateParameterDisplay();
	}

	if (key == 'o' && !madMapperLoadError) {
		for (auto& p : *(*currentPage).getParameters()) {
			std::cout << p->getParameterValue() << endl;
		}
	}

	if (key == OF_KEY_UP && !madMapperLoadError) {
		bankForward(p);
	}
	if (key == OF_KEY_DOWN && !madMapperLoadError) {
		bankBackward(p);
	}

	// Cycle through current page
	if (key == OF_KEY_LEFT && !madMapperLoadError) {
		pageBackward(p);
	}
	if (key == OF_KEY_RIGHT && !madMapperLoadError) {
		pageForward(p);
	}

	if (key > 48 && key < 58) {
		int index = key - 48;
		string name = "solo_" + ofToString(index);
		selectMedia(name);
	}
	if (key == '0') {
		float p = 1;
		backToCurrent(p);
	}
	if ((key == '[' || key == ']') && !madMapperLoadError) {
		cycleCueBank(key == '[' ? -1 : 1);
	}
}

//--------------------------------------------------------------
void ofApp::setActivePage(MadParameterPage* page, MadParameterPage* prevPage) {
	if (page == nullptr) {
		std::lock_guard<std::mutex> lock(activePageMutex);
		activePageName.clear();
		return;
	}
	if (prevPage != nullptr) {
		prevPage->unlinkDevice();
	}
	if(!noDeviceConnected) page->linkDevice();
	{
		std::lock_guard<std::mutex> lock(activePageMutex);
		activePageName = page->getName();
	}

	ofLog() << "Active page set to " << page->getName() << endl;
	
	// call updateParameterDisplay() on initial page set
	updatePageDisplay();
	updateParameterDisplay();
	updateSubpageMediaButtonFeedback();
}

void ofApp::updateSubpageMediaButtonFeedback() {
	if (!surface) return;
	auto* dev = static_cast<ofxMidiDevice*>(surface.get());

	MadParameterPage* sourcePage = nullptr;
	if (currentPage != madOscQuery.pages.end()) {
		sourcePage = &(*currentPage);
	}
	if (sourcePage && sourcePage->isSubpage() && previousPage != madOscQuery.pages.end()) {
		sourcePage = &(*previousPage);
	}

	const std::string activeName = (currentPage != madOscQuery.pages.end()) ? currentPage->getName() : std::string();

	for (int i = 1; i < 17; ++i) {
		auto* parameter = visibleParameterAt(sourcePage, i);
		std::string layerSubpage = surfaceSubpageName(parameter);
		std::string mediaSubpage = (parameter && parameter->isSelectable()) ? parameter->getConnectedMediaName() : std::string();

		std::string surfaceLabel = labelForRoleOrPrefix(dev, "param." + ofToString(i) + ".layerSubpage", "sel_", i);
		if (!surfaceLabel.empty()) {
			auto& c = dev->midiComponents[surfaceLabel];
			float v = (!layerSubpage.empty() && activeName == layerSubpage) ? 1.f : 0.f;
			c.value.disableEvents();
			c.value = v;
			c.update();
			c.value.enableEvents();
		}

		std::string mediaLabel = labelForRoleOrPrefix(dev, "param." + ofToString(i) + ".mediaSubpage", "solo_", i);
		if (!mediaLabel.empty()) {
			auto& c = dev->midiComponents[mediaLabel];
			float v = (!mediaSubpage.empty() && activeName == mediaSubpage) ? 1.f : 0.f;
			c.value.disableEvents();
			c.value = v;
			c.update();
			c.value.enableEvents();
		}
	}
}

//--------------------------------------------------------------
void ofApp::drawStatusString() {
	std::string s = "";
	s += "Current page: " + (*currentPage).getName();
	s += "\nCue bank: " + cueBankName + (cueFollowActiveBank ? " (active)" : " (manual)");
	s += "\nRange: " + ofToString((*currentPage).getRange().first) + " " + ofToString((*currentPage).getRange().second);
	s += "\nParameters on page:";

	int parNum = 1;
	for (auto& p : *(*currentPage).getParameters()) {
		s += "\n";
		if (parNum >= (*currentPage).getRange().first && parNum <= (*currentPage).getRange().second)
			s += "* ";
		else
			s += "  ";
		s += ofToString(parNum) + ") ";
		if (p->isGroup()) s += "G ";
		s += p->oscAddress + " " + ofToString(p->getParameterValue());
		parNum++;
	}
	ofDrawBitmapString(s, 15, 15);
}
//--------------------------------------------------------------
bool ofApp::reloadFromServer(float& p) {
	if (p == 1) {
		try {
		std::lock_guard<std::mutex> lock(oscServers.stateMutex());
		if (!oscServers.reachable(0)) {
			ofLogWarning("ofApp") << "Primary endpoint unreachable - skipping reload";
			madMapperLoadError = true;
			return false;
		}

		// Removed noisy ofLogNotice("ofApp") reload logs.
		// Save previous location
		std::string prevPageName = "";
		int prevLowerBound = 1;
		if (initialised && currentPage != madOscQuery.pages.end()) {
			prevPageName = (*currentPage).getName();
			prevLowerBound = (*currentPage).getRange().first;
		}
		madOscQuery.receive();

		if (madOscQuery.madMapperJson == nullptr) {
			ofLog(OF_LOG_WARNING) << "Reload unsuccessful!" << endl;
			return false;
		}

		// Remove listeners first (uses currentPage — must happen before page clear)
		if (initialised) {
			removeListeners();
		}
		// Always clear pages/groups before rebuild to avoid accumulation
		selectGroup.clear();
		muteGroup.clear();
		soloGroup.clear();
		madOscQuery.pages.clear();
		madOscQuery.subPages.clear();
		currentPage = madOscQuery.pages.end();
		previousPage = madOscQuery.pages.end();
		{
			std::lock_guard<std::mutex> activePageLock(activePageMutex);
			activePageName.clear();
		}

		// Rebuild pages and UI
		const ofJson customPageJson = ofLoadJson("custom_page.json");
		madOscQuery.createCustomPages(static_cast<ofxMidiDevice*>(surface.get()), customPageJson,
									  madOscQuery.madMapperJson, 0);
		// Re-subscribe: the rebuild recreated parameterMap, and on late/re-connect
		// the map was empty (or the socket fresh) when subscribeAllParameters last ran.
		madOscQuery.subscribeAllParameters();

		// Build pages for extra servers and splice into the main page list
		for (size_t i = 0; i < oscServers.extraCount(); ++i) {
			if (!oscServers.reachable(i + 1)) {
				ofLogWarning("ofApp") << "Extra server " << i + 1 << " unreachable, skipping page build";
				continue;
			}
			auto& extraServer = *oscServers.extra(i);
			extraServer.receive(); // refresh data from server
			if (extraServer.madMapperJson.is_null()) {
				ofLogWarning("ofApp") << "Extra server " << i + 1 << " unreachable, skipping page build";
				continue;
			}
			extraServer.pages.clear();
			extraServer.parameterMap.clear();
			extraServer.createCustomPages(static_cast<ofxMidiDevice*>(surface.get()), customPageJson,
										  extraServer.madMapperJson, i + 1);
			extraServer.subscribeAllParameters();
			oscServers.registerPathRouting(i + 1, extraServer);
			const size_t extraPageCount = extraServer.pages.size();
			madOscQuery.pages.splice(madOscQuery.pages.end(), extraServer.pages);
			ofLogNotice("ofApp") << "Built " << extraPageCount << " pages for extra server " << i + 1;
		}

		setupUI(madOscQuery.madMapperJson);

		// Try to restore previous page
		for (auto pageIt = madOscQuery.pages.begin(); pageIt != madOscQuery.pages.end(); ++pageIt) {
			if (pageIt->getName() == prevPageName) {
				currentPage = pageIt;
				setActivePage(&(*currentPage), nullptr);
				(*currentPage).setLowerBound(prevLowerBound);
				ofLog(OF_LOG_NOTICE) << "Reload successful and resuming from " << prevPageName << endl;
				initialised = true;
				return true;
			}
		}
		ofLog(OF_LOG_NOTICE) << "Reload successful assigning to first page!" << endl;
		if (!madOscQuery.pages.empty()) {
			currentPage = madOscQuery.pages.begin();
			setActivePage(&(*currentPage), nullptr);
		}
		initialised = true;
		return true;
		} catch (const std::exception& e) {
			ofLogError("ofApp") << "reloadFromServer: JSON parsing exception: " << e.what();
			madMapperLoadError = true;
			return false;
		} catch (...) {
			ofLogError("ofApp") << "reloadFromServer: unknown exception";
			madMapperLoadError = true;
			return false;
		}
	}
	return false;
}

// Linux rtmidi returns "ClientName:PortName NN:MM"; profile stores just "PortName".
// Substring match handles both Linux and macOS naming.
static bool midiPortMatches(const std::vector<std::string>& ports, const std::string& name) {
	for (const auto& p : ports)
		if (p == name || p.find(name) != std::string::npos) return true;
	return false;
}

//--------------------------------------------------------------
void ofApp::disconnectMidiDevice() {
	if (!surface) return;
	ofLogNotice("ofApp") << "MIDI device disconnected: "
	                     << (activeProfile ? activeProfile->name : "unknown");
	if (initialised && currentPage != madOscQuery.pages.end()) removeListeners();
	// Groups and pages hold pointers into the surface's components — drop them
	// before the surface is destroyed or they dangle after a surface swap.
	selectGroup.clear();
	muteGroup.clear();
	soloGroup.clear();
	for (auto& page : madOscQuery.pages) page.setMidiDevice(nullptr);
	for (auto& page : madOscQuery.subPages) page.setMidiDevice(nullptr);
	surface.reset();
	activeProfile.reset();
	noDeviceConnected = true;
	virtualSurface = false;
	{
		std::lock_guard<std::mutex> lock(displayMutex);
		displaySnapshot = DisplaySnapshot{};
		displayCueGrid = TimelineGridState{};
	}
}

//--------------------------------------------------------------
void ofApp::tryConnectMidiDevice() {
	auto profilesOpt = loadDeviceProfiles("device_profiles.json");
	if (!profilesOpt) return;

	const auto inPorts  = ofxMidiIn().getInPortList();
	const auto outPorts = ofxMidiOut().getOutPortList();
	std::optional<DeviceProfile> found;
	for (const auto& p : *profilesOpt) {
		if (midiPortMatches(inPorts, p.midiInPort) && midiPortMatches(outPorts, p.midiOutPort)) {
			found = p;
			break;
		}
	}

	if (found) {
		// Hardware present: replace a virtual surface, keep an existing real one.
		if (surface && !virtualSurface) return;
		if (surface && virtualSurface) disconnectMidiDevice();
		activeProfile = found;
		virtualSurface = false;
		ofLogNotice("ofApp") << "MIDI device connected: " << activeProfile->name;
	} else {
		if (surface) return; // keep whatever surface is active
		// No hardware: fall back to a virtual surface so the web emulator can
		// substitute the controller (same pipeline, no MIDI ports).
		const DeviceProfile* pick = nullptr;
		for (const auto& p : *profilesOpt)
			if (p.name == emulatorProfileName) { pick = &p; break; }
		if (!pick && !profilesOpt->empty()) pick = &profilesOpt->front();
		if (!pick) return;
		emulatorProfileName = pick->name;
		activeProfile = *pick;
		virtualSurface = true;
		ofLogNotice("ofApp") << "No matching MIDI hardware — virtual surface active: " << activeProfile->name;
	}

	if (activeProfile->name.find("Push") != std::string::npos)
		surface = std::make_unique<Push3Surface>();
	else if (activeProfile->name.find("Platform") != std::string::npos)
		surface = std::make_unique<PlatformMSurface>();
	else
		surface = std::make_unique<Faderport16Surface>();

	static_cast<ofxMidiDevice*>(surface.get())->setupFromProfile(*activeProfile);
	surface->onProfileLoaded(*activeProfile);
	noDeviceConnected = false;
	{
		std::lock_guard<std::mutex> lock(displayMutex);
		displaySnapshot.profileName = activeProfile->name;
		displaySnapshot.isVirtual = virtualSurface;
	}

	// Re-bind to current page and re-wire all listeners if already running.
	// Pages were built against the previous surface — re-point them first.
	if (initialised && !madOscQuery.madMapperJson.is_null()
	    && currentPage != madOscQuery.pages.end()) {
		auto* dev = static_cast<ofxMidiDevice*>(surface.get());
		for (auto& page : madOscQuery.pages) page.setMidiDevice(dev);
		for (auto& page : madOscQuery.subPages) page.setMidiDevice(dev);
		selectGroup.clear();
		muteGroup.clear();
		soloGroup.clear();
		setupUI(madOscQuery.madMapperJson);
		setActivePage(&(*currentPage), nullptr);
	}
}

//--------------------------------------------------------------
void ofApp::exit() {
	if (webServer) {
		webServer->stop();
		webServer.reset();
	}
	if (!madMapperLoadError) removeListeners();
}

//--------------------------------------------------------------
void ofApp::keyReleased(int key) {}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y) {}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button) {}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button) {}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button) {}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y) {}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y) {}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h) {}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg) {}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo) {}

// ============== WebServer API Implementation ==============

void ofApp::setupWebServer() {
	webServer = std::make_unique<WebServer>(8080);
	// Bind API callbacks
	webServer->pagesFetcher = [this]() { return getPages(); };
	webServer->pagesSaver = [this](const ofJson& pages) { savePages(pages); };
	webServer->pageActivator = [this](const std::string& pageName) { requestActivatePageByName(pageName); };
	webServer->parametersFetcher = [this]() { return getParameters(); };
	webServer->configFetcher = [this]() { return getConfig(); };
	webServer->configSaver = [this](const ofJson& config) { saveConfig(config); };

	webServer->mappingsFetcher = [this]() { return getMappingsJson(); };
	webServer->mappingsSaver = [this](const ofJson& body) {
		mappingsJson = body;
		saveMappings();
		loadMappings();
		hasPendingBindingsUpdate.store(true);
	};
	webServer->profilesFetcher = [this]() { return getAllProfilesJson(); };
	webServer->profileFetcher = [this]() { return getProfileJson(); };
	webServer->profileSaver = [this](const ofJson& body) {
		saveProfileJson(body);
		hasPendingBindingsUpdate.store(true);
	};

	webServer->displayFetcher = [this]() { return getDisplayJson(); };
	webServer->midiInjector = [this](const ofJson& body) { injectMidiMessage(body); };
	webServer->emulatorSurfaceSetter = [this](const ofJson& body) { requestEmulatorSurface(body); };

	webServer->learnStarter = [this]() { startLearnMode(); };
	webServer->learnStopper = [this]() { stopLearnMode(); };
	webServer->learnInjector = [this](const ofJson& body) {
		int ch     = body.value("channel", 1);
		int status = body.value("status",  176); // default CC
		int ctrl   = body.value("control", 0);
		int pitch  = body.value("pitch",   0);
		int val    = body.value("value",   0);
		injectLearnMessage(ch, status, ctrl, pitch, val);
	};
	webServer->learnStatusFetcher = [this]() {
		std::lock_guard<std::mutex> lock(learnMutex);
		ofJson j;
		j["active"]  = learnModeActive;
		j["ready"]   = learnedMsg.ready;
		j["channel"] = learnedMsg.channel;
		j["control"] = learnedMsg.control;
		j["status"]  = learnedMsg.status;
		j["pitch"]   = learnedMsg.pitch;
		j["type"]    = learnedMsg.type;
		j["messages"] = ofJson::array();
		for (const auto& m : learnMessages) {
			ofJson entry;
			entry["channel"] = m.channel;
			entry["control"] = m.control;
			entry["status"]  = m.status;
			entry["pitch"]   = m.pitch;
			entry["value"]   = m.value;
			entry["type"]    = m.type;
			j["messages"].push_back(entry);
		}
		return j;
	};
	webServer->learnAssigner = [this](const ofJson& body) {
		// body: { label, channel, control, type, interfaceType, role }
		// Writes a new component + binding into the active device profile JSON.
		if (!activeProfile) return;
		if (!body.contains("label") || !body["label"].is_string()) return;

		const std::string path = ofToDataPath("device_profiles.json", true);
		ofJson profiles;
		try { profiles = ofLoadJson(path); } catch (...) { return; }
		if (!profiles.is_array()) return;

		for (auto& profile : profiles) {
			if (profile.value("name", std::string()) != activeProfile->name) continue;

			// Add or update component
			if (!profile.contains("components") || !profile["components"].is_array())
				profile["components"] = ofJson::array();

			std::string label = body["label"].get<std::string>();
			bool found = false;
			for (auto& comp : profile["components"]) {
				if (comp.value("label", std::string()) == label) {
					if (body.contains("channel"))       comp["channel"]       = body["channel"];
					if (body.contains("address"))       comp["address"]       = body["address"];
					else if (body.contains("control"))  comp["address"]       = body["control"]; // legacy
					if (body.contains("type"))          comp["type"]          = body["type"];
					if (body.contains("interfaceType")) comp["interfaceType"] = body["interfaceType"];
					found = true;
					break;
				}
			}
			if (!found) {
				ofJson comp;
				comp["label"]         = label;
				comp["channel"]       = body.value("channel", 1);
				comp["address"]       = body.contains("address") ? body["address"].get<int>()
				                      : body.value("control", 0);
				comp["type"]          = body.value("type", std::string("cc"));
				comp["interfaceType"] = body.value("interfaceType", std::string("button"));
				profile["components"].push_back(comp);
			}

			// Set role on the component (replaces old role→label bindings map)
			if (body.contains("role") && body["role"].is_string()) {
				const std::string role = body["role"].get<std::string>();
				if (profile.contains("components") && profile["components"].is_array()) {
					// Clear this role from any other component that currently holds it
					for (auto& comp : profile["components"])
						if (comp.value("role", "") == role) comp.erase("role");
					// Set role on the matching component
					for (auto& comp : profile["components"]) {
						if (comp.value("label", "") == label) {
							if (role.empty()) comp.erase("role");
							else comp["role"] = role;
							break;
						}
					}
				}
			}

			ofSavePrettyJson(path, profiles);
			ofLogNotice("ofApp") << "Learn assign saved: " << label << " → " << body.value("role", std::string("(no role)"));
			hasPendingBindingsUpdate.store(true);
			break;
		}
	};

	webServer->start();
}

ofJson ofApp::getPages() {
	// Load and return custom_page.json
	try {
		const std::string filepath = resolveCustomPagesPath();
		ofJson data = ofLoadJson(filepath);
		return ensurePagesShape(data);
	} catch (const std::exception& e) {
		ofLogError() << "Failed to load custom_page.json: " << e.what();
		return ensurePagesShape(ofJson::object());
	}
}

bool ofApp::activatePageByName(const std::string& pageName) {
	if (pageName.empty() || madOscQuery.pages.empty()) return false;
	for (auto pageIt = madOscQuery.pages.begin(); pageIt != madOscQuery.pages.end(); ++pageIt) {
		if (pageIt->getName() != pageName) continue;
		MadParameterPage* prevPage = currentPage != madOscQuery.pages.end() ? &(*currentPage) : nullptr;
		currentPage = pageIt;
		previousPage = currentPage;
		setActivePage(&(*currentPage), prevPage);
		return true;
	}
	ofLogWarning("ofApp") << "activatePageByName: page not found: " << pageName;
	return false;
}

void ofApp::requestActivatePageByName(const std::string& pageName) {
	if (pageName.empty()) return;
	std::lock_guard<std::mutex> lock(pendingPageMutex);
	pendingPageName = pageName;
	hasPendingPageActivation = true;
}

void ofApp::savePages(const ofJson& pages) {
	// Save custom_page.json (async, don't reload to avoid crashes)
	try {
		// Validate structure: pages should have "pages" array
		if (!pages.is_object()) {
			ofLogError() << "savePages: Invalid JSON structure (not an object)";
			return;
		}
		
		if (!pages.contains("pages") || !pages["pages"].is_array()) {
			ofLogError() << "savePages: Missing or invalid 'pages' array";
			return;
		}
		
		// Create a safe copy with validated structure
		ofJson validated = ofJson::object();
		validated["pages"] = pages["pages"];
		if (pages.contains("subpages") && pages["subpages"].is_array()) {
			validated["subpages"] = pages["subpages"];
		}
		
		// Save the validated json
		bool contentChanged = true;
		try {
			ofJson current = ofLoadJson(resolveCustomPagesPath());
			ofJson currentNormalized = ensurePagesShape(current);
			ofJson validatedNormalized = ensurePagesShape(validated);
			contentChanged = (currentNormalized != validatedNormalized);
		} catch (...) {
			contentChanged = true;
		}

		ofSaveJson(resolveCustomPagesPath(), validated);
		ofLogNotice() << "Saved custom_page.json with " << validated["pages"].size() << " pages";

		const bool liveReload = !pages.contains("liveReload") || !pages["liveReload"].is_boolean() || pages["liveReload"].get<bool>();
		if (initialised && liveReload && contentChanged) {
			hasPendingReload.store(true);
		}
		if (pages.contains("currentPage") && pages["currentPage"].is_string()) {
			requestActivatePageByName(pages["currentPage"].get<std::string>());
		}
	} catch (const std::exception& e) {
		ofLogError() << "Failed to save custom_page.json: " << e.what();
	} catch (...) {
		ofLogError() << "Unknown error saving custom_page.json";
	}
}

ofJson ofApp::getParameters() {
	std::lock_guard<std::mutex> lock(oscServers.stateMutex());
	// Return all parameters organized by server
	ofJson result = ofJson::object();
	const auto& configs = oscServers.configs();
	std::vector<std::unordered_set<std::string>> emittedPaths;
	emittedPaths.resize(std::max<size_t>(1, configs.size()));

	// Build server buckets first.
	for (size_t i = 0; i < configs.size(); ++i) {
		std::string serverId = "server_" + std::to_string(i);
		ofJson serverInfo = ofJson::object();
		serverInfo["name"] = configs[i].id;
		serverInfo["id"] = serverId;
		const bool reachable = oscServers.reachable(i);
		const ofxMadOscQuery* query = oscServers.server(i);
		serverInfo["connected"] = reachable && query != nullptr && query->madMapperJson != nullptr;
		serverInfo["reachable"] = reachable;
		serverInfo["parameters"] = ofJson::array();
		result[serverId] = serverInfo;
	}

	if (!result.contains("server_0")) {
		ofJson server0 = ofJson::object();
		server0["name"] = "MadMapper";
		server0["id"] = "server_0";
		server0["connected"] = madOscQuery.madMapperJson != nullptr;
		server0["parameters"] = ofJson::array();
		result["server_0"] = server0;
	}

	auto appendParameter = [&](size_t sid, const ofJson& node) {
		if (sid >= emittedPaths.size()) return;
		auto itFullPath = node.find("FULL_PATH");
		if (itFullPath == node.end() || !itFullPath->is_string()) return;
		const std::string path = itFullPath->get<std::string>();
		if (path.empty()) return;
		const std::string serverId = "server_" + std::to_string(sid);
		if (!result.contains(serverId)) return;
		if (emittedPaths[sid].find(path) != emittedPaths[sid].end()) return;
		emittedPaths[sid].insert(path);

		ofJson parameter = ofJson::object();
		parameter["name"] = oscNodeDisplayName(node, path);
		parameter["display"] = oscNodeDisplayName(node, path);
		parameter["path"] = path;
		result[serverId]["parameters"].push_back(parameter);
	};

	auto collectParameters = [&](auto&& self, size_t sid, const ofJson& node) -> void {
		if (!node.is_object()) return;
		if (isBindableOscType(node)) appendParameter(sid, node);
		auto itContents = node.find("CONTENTS");
		if (itContents == node.end() || !itContents->is_object()) return;
		for (auto it = itContents->begin(); it != itContents->end(); ++it) {
			self(self, sid, it.value());
		}
	};

	if (oscServers.reachable(0) && madOscQuery.madMapperJson.is_object()) {
		collectParameters(collectParameters, 0, madOscQuery.madMapperJson);
	}

	for (size_t i = 0; i < oscServers.extraCount(); ++i) {
		if (!oscServers.reachable(i + 1)) continue;
		auto* extra = oscServers.extra(i);
		if (!extra || !extra->madMapperJson.is_object()) continue;
		collectParameters(collectParameters, i + 1, extra->madMapperJson);
	}
	
	// Add current state info without touching potentially invalid iterators.
	{
		std::lock_guard<std::mutex> pageLock(activePageMutex);
		result["current_page"] = activePageName;
	}
	
	return result;
}

ofJson ofApp::getConfig() {
	std::lock_guard<std::mutex> lock(oscServers.stateMutex());
	// Return current configuration
	ofJson config = ofJson::object();

	const auto& configs = oscServers.configs();
	config["servers"] = ofJson::array();
	for (size_t i = 0; i < configs.size(); ++i) {
		const auto& serverConfig = configs[i];
		ofJson sc = ofJson::object();
		sc["id"] = serverConfig.id;
		sc["ip"] = serverConfig.ip;
		sc["sendPort"] = serverConfig.sendPort;
		sc["feedbackPort"] = serverConfig.feedbackPort;
		sc["queryPort"] = serverConfig.queryPort;
		sc["discovery"] = serverConfig.discovery;
		sc["reachable"] = oscServers.reachable(i);
		config["servers"].push_back(sc);
	}
	// Bonjour discovered: live mDNS scan results filtered to exclude already-configured IPs
	{
		std::set<std::string> configuredIps;
		for (auto& s : configs) configuredIps.insert(s.ip);
		config["bonjourAnnouncements"] = ofJson::array();
		for (auto& svc : oscServers.bonjourSnapshot()) {
			std::string ip = svc.value("ip", std::string());
			if (configuredIps.count(ip) == 0)
				config["bonjourAnnouncements"].push_back(svc);
		}
	}

	{
		std::lock_guard<std::mutex> pageLock(activePageMutex);
		config["currentPage"] = activePageName;
	}
	config["initialized"] = initialised;
	config["noDeviceConnected"] = noDeviceConnected;
	config["virtualSurface"] = virtualSurface;

	if (activeProfile) {
		config["activeProfile"] = activeProfile->name;
	}

	return config;
}

void ofApp::saveConfig(const ofJson& config) {
	if (!config.is_object() || !config.contains("servers") || !config["servers"].is_array()) {
		ofLogError("ofApp") << "saveConfig: invalid payload";
		return;
	}

	{
		std::lock_guard<std::mutex> lock(pendingConfigMutex);
		pendingConfig = config;
		hasPendingConfigUpdate = true;
	}
}

void ofApp::applyPendingServerConfig() {
	ofJson configToApply;
	{
		std::lock_guard<std::mutex> lock(pendingConfigMutex);
		if (!hasPendingConfigUpdate) return;
		configToApply = pendingConfig;
		hasPendingConfigUpdate = false;
	}

	if (!configToApply.is_object() || !configToApply.contains("servers") || !configToApply["servers"].is_array()) {
		ofLogError("ofApp") << "applyPendingServerConfig: invalid payload";
		return;
	}

	settings["servers"] = sanitizeServersArray(configToApply["servers"]);
	if (!settings["servers"].empty()) {
		auto first = settings["servers"][0];
		settings["ip"] = first.value("ip", std::string("127.0.0.1"));
		settings["sendPort"] = first.value("sendPort", PORT_RECEIVE);
		settings["queryPort"] = first.value("queryPort", first.value("sendPort", PORT_RECEIVE));
		settings["feedbackPort"] = first.value("feedbackPort", PORT_FEEDBACK);
	}

	ofSavePrettyJson(resolveSettingsPath(), settings);

	std::vector<OscServerConfig> newConfigs = OscServerConfig::parseList(settings);
	if (newConfigs.empty()) {
		ofLogError("ofApp") << "applyPendingServerConfig: no valid servers";
		return;
	}

	oscServers.applyConfigs(std::move(newConfigs));
	ip = oscServers.configs().front().ip;
	sendPort = oscServers.configs().front().sendPort;
	queryPort = oscServers.configs().front().queryPort;
	feedbackPort = oscServers.configs().front().feedbackPort;

	oscServers.refreshEndpointHealth(true);
	if (oscServers.reachable(0)) {
		std::lock_guard<std::mutex> lock(oscServers.stateMutex());
		if (oscServers.connectPrimary()) {
			subscribeTimelinePaths();
		}
	}

	oscServers.setupAdditionalServers();
	hasPendingReload.store(true);
	{
		auto cpIt = configToApply.find("currentPage");
		if (cpIt != configToApply.end() && cpIt->is_string())
			requestActivatePageByName(cpIt->get<std::string>());
	}
}

void ofApp::updatePageDisplay() {
	if (!surface) return;
	if (currentPage == madOscQuery.pages.end()) return;
	const std::string pageName = currentPage->getName();
	if (pageName.empty()) return;
	{
		std::lock_guard<std::mutex> lock(displayMutex);
		displaySnapshot.page = pageName;
	}
	surface->updatePageDisplay(pageName);
}

void ofApp::rebuildCueGrid(const ofJson& madMapperJson) {
	timelineGridState = TimelineGridState{};
	cueGridActive = false;
	availableCueBanks.clear();
	if (!surface || madMapperJson.is_null()) {
		if (surface) surface->updateTimelineGrid(timelineGridState);
		return;
	}

	CueGridBuilder builder;
	if (activeProfile && activeProfile->grid) {
		builder.rows = activeProfile->grid->rows;
		builder.cols = activeProfile->grid->cols;
		builder.flipTopOrigin = activeProfile->grid->flipTopOrigin;
	}
	builder.configuredBank = cueBankName;
	builder.followActiveBank = cueFollowActiveBank;

	timelineGridState = builder.build(madMapperJson);
	availableCueBanks = builder.availableBanks;
	cueBankName = builder.resolvedBank;
	subscribeTimelinePaths();

	cueGridActive = !timelineGridState.empty();
	{
		std::lock_guard<std::mutex> lock(displayMutex);
		displayCueGrid = timelineGridState;
	}
	surface->updateTimelineGrid(timelineGridState);
}

void ofApp::cycleCueBank(int direction) {
	if (availableCueBanks.empty()) return;
	auto currentIt = std::find(availableCueBanks.begin(), availableCueBanks.end(), cueBankName);
	int currentIndex = currentIt == availableCueBanks.end() ? 0 : static_cast<int>(std::distance(availableCueBanks.begin(), currentIt));
	int nextIndex = (currentIndex + direction + static_cast<int>(availableCueBanks.size())) % static_cast<int>(availableCueBanks.size());
	cueFollowActiveBank = false;
	cueBankName = availableCueBanks[nextIndex];
	rebuildCueGrid(madOscQuery.madMapperJson);
	updateParameterDisplay();
	ofLogNotice("ofApp") << "Cue bank switched to " << cueBankName << " (manual override)";
}

void ofApp::unbindCueGrid() {
	if (!surface || !surface->supportsGrid()) return;
	surface->setGridTriggerHandler({});
}

void ofApp::bindCueGrid() {
	if (!surface || !surface->supportsGrid()) return;
	surface->setGridTriggerHandler([this](const CueGridItem& cue) {
		triggerCue(cue);
	});
}

void ofApp::triggerCue(const CueGridItem& cue) {
	if (cue.oscAddress.empty()) return;
	ofxOscMessage message;
	message.setAddress(cue.oscAddress);
	oscServers.sendTo(oscServers.serverIdForPath(cue.oscAddress), message);
}

void ofApp::onWebSocketPathUpdate(std::string& path) {
	if (path.rfind("/timelines", 0) != 0 && path.rfind("timelines", 0) != 0) return;
	cueGridRefreshPending = true;
}

void ofApp::subscribeTimelinePaths() {
	if (!madOscQuery.isWebSocketConnected()) return;

	ofRemoveListener(madOscQuery.webSocketPathE, this, &ofApp::onWebSocketPathUpdate);
	ofAddListener(madOscQuery.webSocketPathE, this, &ofApp::onWebSocketPathUpdate);

	madOscQuery.subscribeParameter("/timelines/active_bank");
	for (const auto& bank : availableCueBanks) {
		madOscQuery.subscribeParameter("/timelines/" + bank + "/setup");
		madOscQuery.subscribeParameter("/timelines/" + bank + "/by_name");
	}
}

void ofApp::updateParameterDisplay() {
	if (!surface) return;
	if (currentPage == madOscQuery.pages.end()) return;

	std::vector<std::string> labels;
	std::vector<float> values;
	int parNum = 1;
	auto range = (*currentPage).getRange();
	for (auto* p : *(*currentPage).getParameters()) {
		if (parNum >= range.first && parNum <= range.second) {
			std::string label = p->getDisplayParameterName();
			labels.push_back(label);
			values.push_back(p->get()); // normalized 0..1
		}
		parNum++;
	}
	// pad to device page width (8 slots) if needed
	while (labels.size() < 8) {
		labels.push_back("");
		values.push_back(0.f);
	}

	{
		std::lock_guard<std::mutex> lock(displayMutex);
		displaySnapshot.labels = labels;
		displaySnapshot.values = values;
	}
	surface->updateParameterDisplay(labels, values);
}

// ── Live bindings update (no MadMapper re-query) ─────────────────────────────

void ofApp::applyBindingsUpdate() {
	if (!surface || !activeProfile || !initialised) return;
	if (currentPage == madOscQuery.pages.end()) return;

	// Re-read device profile from disk and patch in-memory bindings
	auto profilesOpt = loadDeviceProfiles("device_profiles.json");
	if (!profilesOpt) return;

	auto* dev = static_cast<ofxMidiDevice*>(surface.get());
	for (const auto& p : *profilesOpt) {
		if (p.name == activeProfile->name) {
			// Patch bindings and components without recreating the surface
			dev->bindings = p.bindings;
			for (const auto& [lbl, compDef] : p.components) {
				auto it = dev->midiComponents.find(lbl);
				if (it != dev->midiComponents.end()) {
					// Update MIDI matching fields in case type/channel/address changed
					it->second.channel            = compDef.channel;
					it->second.control            = compDef.address;
					it->second.pitch              = compDef.address;
					it->second.controlMessageType = compDef.type;
				} else {
					// New component: wire it into the device now
					if (compDef.interfaceType == IT_FADER)
						dev->addFader(lbl, compDef.channel, compDef.address, compDef.type);
					else if (compDef.interfaceType == IT_KNOB)
						dev->addKnob(lbl, compDef.channel, compDef.address, compDef.type);
					else if (compDef.interfaceType == IT_BUTTON_LP)
						dev->addButtonLP(lbl, compDef.channel, compDef.address, compDef.type);
					else
						dev->addButton(lbl, compDef.channel, compDef.address, compDef.type);
					dev->midiComponents[lbl].value.setName(lbl);
					dev->parameterGroup.add(dev->midiComponents[lbl].value);
					ofLogNotice("ofApp") << "applyBindingsUpdate: added new component " << lbl;
				}
			}
			activeProfile = p;
			break;
		}
	}

	// Re-run setupUI with current MadMapper JSON so listeners reflect new bindings
	removeListeners();
	setupUI(madOscQuery.madMapperJson);
	ofLogNotice("ofApp") << "Bindings updated and UI re-wired";
}

// ── Mappings ──────────────────────────────────────────────────────────────────

void ofApp::loadMappings() {
	const std::string path = ofToDataPath("mappings.json", true);
	try {
		mappingsJson = ofLoadJson(path);
	} catch (...) {
		mappingsJson = ofJson::object();
	}
	if (!mappingsJson.is_object()) mappingsJson = ofJson::object();

	// Parse fixed mappings
	fixedMappings.clear();
	if (mappingsJson.contains("fixed") && mappingsJson["fixed"].is_object()) {
		for (auto& [key, val] : mappingsJson["fixed"].items()) {
			if (!val.is_object()) continue;
			FixedMapping fm;
			if (val.contains("path") && val["path"].is_string())
				fm.path = val["path"].get<std::string>();
			if (val.contains("serverId") && val["serverId"].is_number())
				fm.serverId = val["serverId"].get<size_t>();
			if (val.contains("mode") && val["mode"].is_string())
				fm.mode = val["mode"].get<std::string>();
			fixedMappings[key] = fm;
		}
	}

	// The hover encoder is always relative; mappings.json overrides path/server.
	// Without an entry, synthesize one from the legacy settings.json defaults so
	// it wires up like any other delta-mode fixed binding.
	auto it = fixedMappings.find("tdHoverEncoder");
	if (it != fixedMappings.end()) {
		it->second.mode = "delta";
		tdHoverEncoderOscPath = it->second.path;
		tdServerId = it->second.serverId;
	} else if (tdServerId != SIZE_MAX) {
		FixedMapping fm;
		fm.path = tdHoverEncoderOscPath;
		fm.serverId = tdServerId;
		fm.mode = "delta";
		fixedMappings["tdHoverEncoder"] = fm;
	}

	// Parse page goto entries
	pageGotoEntries.clear();
	if (mappingsJson.contains("nav") && mappingsJson["nav"].is_object()) {
		const auto& nav = mappingsJson["nav"];
		if (nav.contains("pageGoto") && nav["pageGoto"].is_array()) {
			const auto& arr = nav["pageGoto"];
			for (int idx = 0, n = static_cast<int>(arr.size()); idx < n; ++idx) {
				PageGotoEntry e;
				e.index    = idx;
				e.role     = "nav.pageGoto." + std::to_string(idx);
				e.pageName = arr[idx].is_string() ? arr[idx].get<std::string>() : "";
				pageGotoEntries.push_back(e);
			}
		}
	}
}

void ofApp::saveMappings() {
	const std::string path = ofToDataPath("mappings.json", true);
	try {
		ofSavePrettyJson(path, mappingsJson);
	} catch (const std::exception& e) {
		ofLogError("ofApp") << "saveMappings failed: " << e.what();
	}
}

ofJson ofApp::getMappingsJson() const {
	ofJson result = mappingsJson;

	// Build pages list: prefer live madOscQuery.pages (populated once MM connects),
	// fall back to custom_page.json so the dropdown works before MM is connected.
	result["pages"] = ofJson::array();
	if (!madOscQuery.pages.empty()) {
		for (auto& page : madOscQuery.pages)
			result["pages"].push_back(const_cast<MadParameterPage&>(page).getName());
	} else {
		try {
			ofJson cpj = ofLoadJson(resolveCustomPagesPath());
			if (cpj.contains("pages") && cpj["pages"].is_array()) {
				for (auto& p : cpj["pages"]) {
					if (p.contains("name") && p["name"].is_string())
						result["pages"].push_back(p["name"].get<std::string>());
				}
			}
		} catch (...) {}
	}
	return result;
}

// ── MIDI Learn ────────────────────────────────────────────────────────────────

void ofApp::startLearnMode() {
	if (!surface) return;
	std::lock_guard<std::mutex> lock(learnMutex);
	learnedMsg = LearnedMessage{};
	learnMessages.clear();
	if (learnModeActive) return;

	learnModeActive = true;

	auto* dev = static_cast<ofxMidiDevice*>(surface.get());
	learnListener.callback = [this](ofxMidiMessage& msg) { onLearnMessage(msg); };
	dev->midiIn.addListener(&learnListener);
	ofLogNotice("ofApp") << "MIDI learn mode started";
}

void ofApp::stopLearnMode() {
	if (!surface) return;
	std::lock_guard<std::mutex> lock(learnMutex);
	if (!learnModeActive) return;

	auto* dev = static_cast<ofxMidiDevice*>(surface.get());
	dev->midiIn.removeListener(&learnListener);
	learnListener.callback = nullptr;
	learnModeActive = false;
	ofLogNotice("ofApp") << "MIDI learn mode stopped";
}

static std::string midiTypeString(int status) {
	if (status == MIDI_CONTROL_CHANGE)                   return "cc";
	if (status == MIDI_NOTE_ON || status == MIDI_NOTE_OFF) return "note";
	if (status == MIDI_PITCH_BEND)                       return "pitch_bend";
	return "unknown";
}

static void addLearnMessage(std::vector<ofApp::LearnedMessage>& log, ofApp::LearnedMessage m) {
	// Deduplicate: if same type+channel+control/pitch already in log, update its value
	for (auto& existing : log) {
		if (existing.status == m.status && existing.channel == m.channel
		    && existing.control == m.control && existing.pitch == m.pitch) {
			existing.value = m.value;
			return;
		}
	}
	log.push_back(m);
	if (log.size() > 20) log.erase(log.begin());
}

void ofApp::onLearnMessage(ofxMidiMessage& msg) {
	std::lock_guard<std::mutex> lock(learnMutex);
	if (!learnModeActive) return;

	LearnedMessage m;
	m.ready   = true;
	m.channel = msg.channel;
	m.status  = msg.status;
	m.control = msg.control;
	m.pitch   = msg.pitch;
	m.value   = msg.value;
	m.type    = midiTypeString(msg.status);

	addLearnMessage(learnMessages, m);
	learnedMsg = m;

	ofLogNotice("ofApp") << "MIDI learn: ch " << msg.channel
	                     << " " << m.type << " ctrl " << msg.control
	                     << " val " << msg.value;
}

void ofApp::injectLearnMessage(int channel, int status, int control, int pitch, int value) {
	std::lock_guard<std::mutex> lock(learnMutex);
	if (!learnModeActive) return;

	LearnedMessage m;
	m.ready   = true;
	m.channel = channel;
	m.status  = status;
	m.control = control;
	m.pitch   = pitch;
	m.value   = value;
	m.type    = midiTypeString(status);

	addLearnMessage(learnMessages, m);
	learnedMsg = m;
}

// ── Emulator bridge ───────────────────────────────────────────────────────────

ofJson ofApp::getDisplayJson() {
	std::lock_guard<std::mutex> lock(displayMutex);
	ofJson j = ofJson::object();
	j["page"] = displaySnapshot.page;
	j["profile"] = displaySnapshot.profileName;
	j["virtual"] = displaySnapshot.isVirtual;
	j["labels"] = displaySnapshot.labels;
	j["values"] = displaySnapshot.values;

	ofJson grid = ofJson::object();
	grid["rows"] = displayCueGrid.rows;
	grid["cols"] = displayCueGrid.cols;
	grid["bank"] = displayCueGrid.bankName;
	grid["cells"] = ofJson::array();
	for (const auto& cell : displayCueGrid.cells) {
		ofJson c = ofJson::object();
		c["row"] = cell.row;
		c["col"] = cell.column;
		c["name"] = cell.name;
		char hex[8];
		snprintf(hex, sizeof(hex), "#%02X%02X%02X", cell.color.r, cell.color.g, cell.color.b);
		c["color"] = hex;
		c["isPlaying"] = cell.isPlaying;
		grid["cells"].push_back(c);
	}
	j["cueGrid"] = grid;
	return j;
}

void ofApp::injectMidiMessage(const ofJson& body) {
	// Called on the WebServer thread — only enqueue here. The message is
	// delivered to the surface on the main thread in drainInjectedMidi(),
	// so it can't race with page changes or a surface swap.
	ofxMidiMessage msg;
	msg.status   = static_cast<MidiStatus>(body.value("status", 176));
	msg.channel  = body.value("channel", 1);
	msg.control  = body.value("control", 0);
	msg.pitch    = body.value("pitch", 0);
	msg.value    = body.value("value", 0);
	msg.velocity = body.value("value", 0);
	std::lock_guard<std::mutex> lock(midiInjectMutex);
	injectedMidiQueue.push_back(msg);
}

void ofApp::drainInjectedMidi() {
	std::vector<ofxMidiMessage> pending;
	{
		std::lock_guard<std::mutex> lock(midiInjectMutex);
		if (injectedMidiQueue.empty()) return;
		pending.swap(injectedMidiQueue);
	}
	if (!surface) return;
	auto* dev = static_cast<ofxMidiDevice*>(surface.get());
	for (auto& msg : pending) dev->newMidiMessage(msg);
}

void ofApp::requestEmulatorSurface(const ofJson& body) {
	if (!body.is_object()) return;
	auto it = body.find("profile");
	if (it == body.end() || !it->is_string()) return;
	std::lock_guard<std::mutex> lock(emulatorMutex);
	pendingEmulatorProfile = it->get<std::string>();
	hasPendingEmulatorSwitch.store(true);
}

// ── Profile fetch/save for mapping UI ────────────────────────────────────────

ofJson ofApp::getAllProfilesJson() {
	try {
		ofJson profiles = ofLoadJson(ofToDataPath("device_profiles.json", true));
		if (!profiles.is_array()) return ofJson::array();
		// Annotate each profile with whether it's currently active
		const std::string activeName = activeProfile ? activeProfile->name : "";
		for (auto& p : profiles)
			p["active"] = (p.value("name", std::string()) == activeName);
		return profiles;
	} catch (const std::exception& e) {
		ofLogError("ofApp") << "getAllProfilesJson: " << e.what();
		return ofJson::array();
	}
}

ofJson ofApp::getProfileJson() {
	if (!activeProfile) return ofJson::object();
	// Re-read the file so the UI always sees the current on-disk state
	try {
		ofJson profiles = ofLoadJson(ofToDataPath("device_profiles.json", true));
		if (!profiles.is_array()) return ofJson::object();
		for (auto& profile : profiles) {
			if (profile.value("name", std::string()) == activeProfile->name)
				return profile;
		}
	} catch (const std::exception& e) {
		ofLogError("ofApp") << "getProfileJson: " << e.what();
	}
	return ofJson::object();
}

void ofApp::saveProfileJson(const ofJson& updated) {
	try {
		const std::string path = ofToDataPath("device_profiles.json", true);
		ofJson profiles = ofLoadJson(path);
		if (!profiles.is_array()) return;
		std::string targetName = updated.value("name", std::string());
		for (auto& profile : profiles) {
			if (profile.value("name", std::string()) == targetName) {
				profile = updated;
				ofSavePrettyJson(path, profiles);
				ofLogNotice("ofApp") << "Saved profile: " << targetName;
				return;
			}
		}
		ofLogWarning("ofApp") << "saveProfileJson: profile not found: " << targetName;
	} catch (const std::exception& e) {
		ofLogError("ofApp") << "saveProfileJson: " << e.what();
	}
}


