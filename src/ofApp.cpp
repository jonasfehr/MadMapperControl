#include "ofApp.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <sstream>

static const ofJson* jsonGet(const ofJson& root, std::initializer_list<const char*> keys);

namespace {
	std::optional<int> jsonIntValue(const ofJson& node, std::initializer_list<const char*> keys) {
		for (auto key : keys) {
			auto it = node.find(key);
			if (it == node.end()) continue;
			if (it->is_number_integer()) return it->get<int>();
			if (it->is_number()) return static_cast<int>(std::round(it->get<double>()));
			if (it->is_string()) {
				try {
					return ofToInt(it->get<std::string>());
				} catch (...) {
				}
			}
		}
		return std::nullopt;
	}

	std::optional<std::string> jsonStringValue(const ofJson& node, std::initializer_list<const char*> keys) {
		for (auto key : keys) {
			auto it = node.find(key);
			if (it != node.end() && it->is_string()) return it->get<std::string>();
		}
		return std::nullopt;
	}

	std::string firstValueString(const ofJson& node) {
		auto it = node.find("VALUE");
		if (it == node.end() || !it->is_array() || it->empty() || !(*it)[0].is_string()) return std::string();
		return (*it)[0].get<std::string>();
	}

	std::optional<ofJson> firstValueJson(const ofJson& node) {
		auto it = node.find("VALUE");
		if (it == node.end() || !it->is_array() || it->empty()) return std::nullopt;
		return (*it)[0];
	}

	std::vector<std::string> timelineBankNames(const ofJson& root) {
		std::vector<std::string> names;
		auto* timelinesNode = jsonGet(root, {"CONTENTS", "timelines", "CONTENTS"});
		if (!timelinesNode || !timelinesNode->is_object()) return names;
		for (auto it = timelinesNode->begin(); it != timelinesNode->end(); ++it) {
			if (!it.value().is_object()) continue;
			if (it.key() == "editor" || it.key() == "active_bank") continue;

			auto contentsIt = it.value().find("CONTENTS");
			if (contentsIt == it.value().end() || !contentsIt->is_object()) continue;

			bool hasSetup = contentsIt->find("setup") != contentsIt->end();
			bool hasByName = contentsIt->find("by_name") != contentsIt->end();
			if (hasSetup && hasByName) names.push_back(it.key());
		}
		std::sort(names.begin(), names.end());
		return names;
	}

	const ofJson* resolveNodeByPath(const ofJson& root, const std::string& path) {
		if (path.empty() || path[0] != '/') return nullptr;
		const ofJson* node = &root;
		for (auto& token : ofSplitString(path, "/", true, true)) {
			if (!node->is_object()) return nullptr;
			auto contentsIt = node->find("CONTENTS");
			if (contentsIt != node->end() && contentsIt->is_object()) {
				auto childIt = contentsIt->find(token);
				if (childIt != contentsIt->end()) {
					node = &(*childIt);
					continue;
				}
			}
			auto directIt = node->find(token);
			if (directIt == node->end()) return nullptr;
			node = &(*directIt);
		}
		return node;
	}

	std::optional<ofJson> parseCueSetupValue(const ofJson& root,
									 const ofJson* bankContents,
									 std::string raw,
									 int depth = 0) {
		if (depth > 4) return std::nullopt;
		raw = ofTrim(raw);
		if (raw.empty() || raw == "[]" || raw == "{}") return std::nullopt;

		if (raw.front() == '[' || raw.front() == '{') {
			return ofJson::parse(raw);
		}

		if (raw.front() == '"') {
			auto parsed = ofJson::parse(raw);
			if (parsed.is_string()) {
				return parseCueSetupValue(root, bankContents, parsed.get<std::string>(), depth + 1);
			}
			if (parsed.is_array() || parsed.is_object()) return parsed;
		}

		if (raw.front() == '/') {
			auto* referenced = resolveNodeByPath(root, raw);
			if (!referenced) return std::nullopt;
			auto referencedValue = firstValueString(*referenced);
			if (!referencedValue.empty()) {
				return parseCueSetupValue(root, bankContents, referencedValue, depth + 1);
			}
			if (referenced->is_array() || referenced->is_object()) return *referenced;
			return std::nullopt;
		}

		if (bankContents && bankContents->is_object()) {
			auto it = bankContents->find(raw);
			if (it != bankContents->end()) {
				auto referencedValue = firstValueString(*it);
				if (!referencedValue.empty()) {
					return parseCueSetupValue(root, bankContents, referencedValue, depth + 1);
				}
				if (it->is_array() || it->is_object()) return *it;
			}
		}

		auto* editorContents = jsonGet(root, {"CONTENTS", "timelines", "CONTENTS", "editor", "CONTENTS"});
		if (editorContents && editorContents->is_object()) {
			auto editorIt = editorContents->find(raw);
			if (editorIt != editorContents->end()) {
				auto referencedValue = firstValueString(*editorIt);
				if (!referencedValue.empty()) {
					return parseCueSetupValue(root, bankContents, referencedValue, depth + 1);
				}
				if (editorIt->is_array() || editorIt->is_object()) return *editorIt;
			}
		}

		return std::nullopt;
	}

	std::string resolveCueBankName(const ofJson& root,
							  const std::vector<std::string>& availableBanks,
							  const std::string& configuredBank,
							  bool followActiveBank) {
		auto containsBank = [&](const std::string& bankName) {
			return std::find(availableBanks.begin(), availableBanks.end(), bankName) != availableBanks.end();
		};

		if (followActiveBank) {
			auto* activeBankNode = jsonGet(root, {"CONTENTS", "timelines", "CONTENTS", "active_bank"});
			auto activeBank = activeBankNode ? firstValueString(*activeBankNode) : std::string();
			if (!activeBank.empty() && containsBank(activeBank)) return activeBank;
		}

		if (!configuredBank.empty() && containsBank(configuredBank)) return configuredBank;
		return availableBanks.empty() ? std::string() : availableBanks.front();
	}

	int normalizeCueIndex(int index) {
		if (index >= 1 && index <= 8) return index - 1;
		return index;
	}

	unsigned char jsonColorComponent(const ofJson& value) {
		if (!value.is_number()) return 0;
		double component = value.get<double>();
		if (component <= 1.0) component *= 255.0;
		return static_cast<unsigned char>(ofClamp(std::round(component), 0.0, 255.0));
	}

	ofColor parseCueColorValue(const ofJson& value) {
		if (value.is_array() && value.size() >= 3) {
			return ofColor(jsonColorComponent(value[0]),
						   jsonColorComponent(value[1]),
						   jsonColorComponent(value[2]));
		}
		if (value.is_object()) {
			auto red = jsonIntValue(value, {"r", "red"});
			auto green = jsonIntValue(value, {"g", "green"});
			auto blue = jsonIntValue(value, {"b", "blue"});
			if (red && green && blue) {
				return ofColor(static_cast<unsigned char>(*red), static_cast<unsigned char>(*green), static_cast<unsigned char>(*blue));
			}
		}
		if (value.is_number_integer()) {
			auto rgb = static_cast<uint32_t>(value.get<int>());
			return ofColor(static_cast<unsigned char>((rgb >> 16) & 0xFF),
						   static_cast<unsigned char>((rgb >> 8) & 0xFF),
						   static_cast<unsigned char>(rgb & 0xFF));
		}
		if (value.is_string()) {
			auto colorText = ofToLower(value.get<std::string>());
			if (!colorText.empty() && colorText[0] == '#') {
				return ofColor::fromHex(ofHexToInt(colorText.substr(1)));
			}
			if (colorText.rfind("0x", 0) == 0) {
				return ofColor::fromHex(ofHexToInt(colorText.substr(2)));
			}
			if (colorText == "red") return ofColor(255, 0, 0);
			if (colorText == "green") return ofColor(0, 255, 0);
			if (colorText == "blue") return ofColor(0, 128, 255);
			if (colorText == "yellow") return ofColor(255, 220, 0);
			if (colorText == "orange") return ofColor(255, 140, 0);
			if (colorText == "white") return ofColor::white;
		}
		return ofColor::white;
	}

	ofColor parseCueColor(const ofJson& node) {
		for (auto key : {"color", "colour", "fill", "rgb"}) {
			auto it = node.find(key);
			if (it != node.end()) return parseCueColorValue(*it);
		}
		auto red = jsonIntValue(node, {"r", "red"});
		auto green = jsonIntValue(node, {"g", "green"});
		auto blue = jsonIntValue(node, {"b", "blue"});
		if (red && green && blue) {
			return ofColor(static_cast<unsigned char>(*red), static_cast<unsigned char>(*green), static_cast<unsigned char>(*blue));
		}
		return ofColor::white;
	}

	bool tryBuildCueItem(const ofJson& node,
					 const std::string& fallbackName,
					 const std::unordered_map<std::string, std::string>& addressByName,
					 const std::string& fallbackOscPrefix,
					 int gridRows,
					 bool flipTopOrigin,
					 CueGridItem& outCue) {
		if (!node.is_object()) return false;

		auto name = jsonStringValue(node, {"name", "cue_name", "cue", "id", "label", "title"});
		std::string cueName = name ? *name : fallbackName;
		if (cueName.empty()) return false;

		auto column = jsonIntValue(node, {"column", "col", "x"});
		auto row = jsonIntValue(node, {"row", "line", "y"});
		auto posIt = node.find("position");
		if ((!column || !row) && posIt != node.end() && posIt->is_object()) {
			if (!column) column = jsonIntValue(*posIt, {"column", "col", "x"});
			if (!row) row = jsonIntValue(*posIt, {"row", "line", "y"});
		}
		if (!column || !row) return false;

		outCue.name = cueName;
		outCue.column = normalizeCueIndex(*column);
		int mappedRow = normalizeCueIndex(*row);
		if (flipTopOrigin && gridRows > 0) {
			mappedRow = (gridRows - 1) - mappedRow;
		}
		outCue.row = mappedRow;
		outCue.color = parseCueColor(node);
		outCue.isPlaying = node.value("is_playing", false);
		outCue.isLastStarted = node.value("is_last_started", false);
		auto oscIt = addressByName.find(cueName);
		outCue.oscAddress = oscIt != addressByName.end()
			? oscIt->second
			: fallbackOscPrefix + "/" + cueName + "/play_from_beginning";
		return outCue.isValid();
	}

	void collectCueItems(const ofJson& node,
					 const std::string& fallbackName,
					 const std::unordered_map<std::string, std::string>& addressByName,
					 const std::string& fallbackOscPrefix,
					 int gridRows,
					 bool flipTopOrigin,
					 std::vector<CueGridItem>& cues) {
		if (node.is_array()) {
			for (const auto& entry : node) {
				collectCueItems(entry, fallbackName, addressByName, fallbackOscPrefix, gridRows, flipTopOrigin, cues);
			}
			return;
		}
		if (!node.is_object()) return;

		CueGridItem cue;
		if (tryBuildCueItem(node, fallbackName, addressByName, fallbackOscPrefix, gridRows, flipTopOrigin, cue)) {
			cues.push_back(cue);
			return;
		}

		for (auto it = node.begin(); it != node.end(); ++it) {
			collectCueItems(it.value(), it.key(), addressByName, fallbackOscPrefix, gridRows, flipTopOrigin, cues);
		}
	}

	std::string normalizePageKey(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
			if (c == ' ') return '_';
			return static_cast<char>(std::tolower(c));
		});
		return value;
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

	std::vector<ofApp::OscServerConfig> parseOscServerConfigs(const ofJson& settings) {
		std::vector<ofApp::OscServerConfig> configs;

		if (settings.contains("servers") && settings["servers"].is_array()) {
			size_t index = 0;
			for (const auto& item : settings["servers"]) {
				if (!item.is_object()) continue;
				ofApp::OscServerConfig cfg;
				cfg.id = item.value("id", "server_" + ofToString(index));
				cfg.ip = item.value("ip", std::string("127.0.0.1"));
				cfg.sendPort = item.value("sendPort", PORT_RECEIVE);
				cfg.feedbackPort = item.value("feedbackPort", PORT_FEEDBACK);
				cfg.queryPort = item.value("queryPort", cfg.sendPort);
				configs.push_back(cfg);
				++index;
			}
		}

		if (configs.empty()) {
			ofApp::OscServerConfig legacy;
			legacy.id = "server_0";
			legacy.ip = settings.contains("ip") ? settings["ip"].get<std::string>() : "127.0.0.1";
			legacy.sendPort = settings.contains("sendPort") ? settings["sendPort"].get<int>() : PORT_RECEIVE;
			legacy.queryPort = settings.contains("queryPort") ? settings["queryPort"].get<int>() : legacy.sendPort;
			legacy.feedbackPort = settings.contains("feedbackPort") ? settings["feedbackPort"].get<int>() : PORT_FEEDBACK;
			configs.push_back(legacy);
		}

		return configs;
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
	oscServerConfigs = parseOscServerConfigs(settings);
	ip = oscServerConfigs.front().ip;
	sendPort = oscServerConfigs.front().sendPort;
	queryPort = oscServerConfigs.front().queryPort;
	feedbackPort = oscServerConfigs.front().feedbackPort;
	if (settings.contains("cueBankName") && settings["cueBankName"].is_string()) {
		cueBankName = settings["cueBankName"].get<std::string>();
	}
	if (settings.contains("cueFollowActiveBank") && settings["cueFollowActiveBank"].is_boolean()) {
		cueFollowActiveBank = settings["cueFollowActiveBank"].get<bool>();
	}

	// Load profiles and select first matching connected device
	auto profilesOpt = loadDeviceProfiles("device_profiles.json");
	if (profilesOpt) {
		const auto inPorts = ofxMidiIn().getInPortList();
		const auto outPorts = ofxMidiOut().getOutPortList();
		for (const auto& p : *profilesOpt) {
			bool inMatch = std::find(inPorts.begin(), inPorts.end(), p.midiInPort) != inPorts.end();
			bool outMatch = std::find(outPorts.begin(), outPorts.end(), p.midiOutPort) != outPorts.end();
			if (inMatch && outMatch) {
				activeProfile = p;
				break;
			}
		}
	}

	if (activeProfile) {
		noDeviceConnected = false;
		ofLogNotice() << "Using MIDI profile: " << activeProfile->name
					  << " (in='" << activeProfile->midiInPort
					  << "', out='" << activeProfile->midiOutPort << "')";
		if (activeProfile->name.find("Push") != std::string::npos)
			surface = std::make_unique<Push3Surface>();
		else if (activeProfile->name.find("Platform") != std::string::npos)
			surface = std::make_unique<PlatformMSurface>();
		else
			surface = std::make_unique<Faderport16Surface>();

		static_cast<ofxMidiDevice*>(surface.get())->setupFromProfile(*activeProfile);
		// Run device-specific initialization (e.g., Push display) after profile setup.
		surface->onProfileLoaded(*activeProfile);
	} else {
		noDeviceConnected = true;
		ofLogWarning() << "No MIDI device/profile matched. Running without controller.";
		surface.reset();
	}

	madOscQuery.setup(ip, sendPort, feedbackPort, queryPort);
	gui.setup();

	madOscQuery.receive();
	ofSleepMillis(100);
	if (madOscQuery.madMapperJson == nullptr) {
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
				registerServerPathRouting(0, madOscQuery);
				subscribeTimelinePaths();
				ofLogNotice() << "OSCQuery WebSocket connected on port " << queryPort;
			}
			setupAdditionalOscServers();

			// Pages were initially built before additional servers existed.
			// Rebuild once so serverId>0 pages are available at startup.
			float rebuildAllPages = 1.f;
			reloadFromServer(rebuildAllPages);
		}
		initialised = true;
	}
	errorImage.load("debug.png");

	// Setup web server for REST API
	setupWebServer();
}

ofxMadOscQuery* ofApp::getOscServer(size_t serverId) {
	if (serverId == 0) return &madOscQuery;
	const size_t extraIndex = serverId - 1;
	if (extraIndex >= extraOscQueries.size()) return nullptr;
	return extraOscQueries[extraIndex].get();
}

const ofxMadOscQuery* ofApp::getOscServer(size_t serverId) const {
	if (serverId == 0) return &madOscQuery;
	const size_t extraIndex = serverId - 1;
	if (extraIndex >= extraOscQueries.size()) return nullptr;
	return extraOscQueries[extraIndex].get();
}

void ofApp::registerServerPathRouting(size_t serverId, const ofxMadOscQuery& server) {
	for (const auto& kv : server.parameterMap) {
		oscPathServerRouting.emplace(kv.first, serverId);
	}
}

size_t ofApp::serverForOscPath(const std::string& oscPath) const {
	auto it = oscPathServerRouting.find(oscPath);
	if (it != oscPathServerRouting.end()) return it->second;
	return 0;
}

void ofApp::oscSendToServer(size_t serverId, ofxOscMessage& message) {
	if (auto* server = getOscServer(serverId)) {
		server->oscSendToMadMapper(message);
		return;
	}
	madOscQuery.oscSendToMadMapper(message);
}

void ofApp::setupAdditionalOscServers() {
	extraOscQueries.clear();
	if (oscServerConfigs.size() <= 1) return;

	for (size_t i = 1; i < oscServerConfigs.size(); ++i) {
		const auto& cfg = oscServerConfigs[i];
		auto server = std::make_unique<ofxMadOscQuery>();
		server->setup(cfg.ip, cfg.sendPort, cfg.feedbackPort, cfg.queryPort);
		server->receive();

		if (server->madMapperJson.is_null()) {
			ofLogWarning("ofApp") << "OSCQuery server '" << cfg.id << "' unreachable at "
									 << cfg.ip << ":" << cfg.queryPort;
			extraOscQueries.push_back(std::move(server));
			continue;
		}

		if (server->connectWebSocket(cfg.queryPort)) {
			server->subscribeAllParameters();
			registerServerPathRouting(i, *server);
			ofLogNotice("ofApp") << "Additional OSCQuery server connected: " << cfg.id
									  << " (" << cfg.ip << ":" << cfg.queryPort << ")";
		} else {
			ofLogWarning("ofApp") << "Additional OSCQuery WebSocket failed: " << cfg.id
									 << " (" << cfg.ip << ":" << cfg.queryPort << ")";
		}

		extraOscQueries.push_back(std::move(server));
	}
}

// CALBACK FUNCTIONS
// --------------------------------------------------------
void ofApp::selectSurface(string& name) {
	if (surface) {
		auto* dev = static_cast<ofxMidiDevice*>(surface.get());
		if (isComponentMappedToRole(dev, name, ".mediaSubpage")) return;
		auto mit = dev->midiComponents.find(name);
		if (mit != dev->midiComponents.end() && mit->second.value.get() < 0.5f) return;
	}
	
	// If already on a subpage and button is pressed, go back to main page
	if ((*currentPage).isSubpage()) {
		float p = 1.f;
		backToCurrent(p);
		return;
	}
	
	auto result = ofSplitString(name, "_");
	if (result.empty()) return;
	int index = ofToInt(result.back());
	MadParameterPage* sourcePage = &(*currentPage);
	if ((*currentPage).isSubpage() && previousPage != madOscQuery.pages.end()) {
		sourcePage = &(*previousPage);
	}
	MadParameter* parameter = visibleParameterAt(sourcePage, index);
	if (!parameter) return;
	if (parameter->isSelectable()) {
		auto oscAddress = ofSplitString(parameter->getOscAddress(), "/");
		int i = 0;
		while (i < (int)oscAddress.size() && oscAddress[i] != "opacity") {
			i++;
		}
		if (i == 0 || i >= (int)oscAddress.size()) return;
		string subpageName = oscAddress[i - 1];

		if ((*currentPage).isSubpage() && normalizePageKey(currentPage->getName()) == normalizePageKey(subpageName)) {
			if (pageContainsOscPrefix(&(*currentPage), "/surfaces/")) {
				float p = 1.f;
				backToCurrent(p);
				return;
			}
		}

		previousPage = currentPage;
		if (auto* target = findSubPageByNameAndPrefix(madOscQuery.subPages, subpageName, "/surfaces/")) {
			MadParameterPage* prevPage = &(*currentPage);
			for (auto pageIt = madOscQuery.subPages.begin(); pageIt != madOscQuery.subPages.end(); ++pageIt) {
				if (&(*pageIt) == target) {
					currentPage = pageIt;
					setActivePage(&(*currentPage), prevPage);
					return;
				}
			}
		}
		oscSelectSurface(subpageName);
	}
}

void ofApp::selectGroupContent(string& name) {
	if (surface) {
		auto* dev = static_cast<ofxMidiDevice*>(surface.get());
		auto mit = dev->midiComponents.find(name);
		if (mit != dev->midiComponents.end() && mit->second.value.get() < 0.5f) return;
	}
	auto result = ofSplitString(name, "_");
	if (result.empty()) return;
	int index = ofToInt(result.back());
	if (currentPage->getParameters()->size() < index) return;
	auto parameter = currentPage->getParameters()->begin();
	std::advance(parameter, index - 1);
	if ((*parameter)->isSelectable() && (*parameter)->isGroup()) {
		auto oscAddress = ofSplitString((*parameter)->getOscAddress(), "/");
		if (oscAddress.size() < 3) return;
		string subpageName = oscAddress[2];
		if ((*currentPage).isSubpage()) return;
		previousPage = currentPage;
		for (auto pageIt = madOscQuery.subPages.begin(); pageIt != madOscQuery.subPages.end(); ++pageIt) {
			if (pageIt->getName() == subpageName + "_SubPage") {
				MadParameterPage* prevPage = &(*currentPage);
				currentPage = pageIt;
				setActivePage(&(*currentPage), prevPage);
				return;
			}
		}
		oscSelectSurface(subpageName);
	}
}

void ofApp::selectMedia(string& name) {
	if (surface) {
		auto* dev = static_cast<ofxMidiDevice*>(surface.get());
		if (isComponentMappedToRole(dev, name, ".surfaceSubpage")) return;
		auto mit = dev->midiComponents.find(name);
		if (mit != dev->midiComponents.end() && mit->second.value.get() < 0.5f) return;
	}
	
	// If already on a subpage and button is pressed, go back to main page
	if ((*currentPage).isSubpage()) {
		float p = 1.f;
		backToCurrent(p);
		return;
	}
	
	// Find the name of the corresponding surface
	auto result = ofSplitString(name, "_");
	if (result.empty()) return;
	int index = ofToInt(result.back());
	MadParameterPage* sourcePage = &(*currentPage);
	if ((*currentPage).isSubpage() && previousPage != madOscQuery.pages.end()) {
		sourcePage = &(*previousPage);
	}
	MadParameter* parameter = visibleParameterAt(sourcePage, index);
	if (!parameter) return;
	if (parameter->isSelectable()) {
		string subpageName = parameter->getConnectedMediaName();
		if (subpageName.empty()) return;

		if ((*currentPage).isSubpage() && normalizePageKey(currentPage->getName()) == normalizePageKey(subpageName)) {
			if (pageContainsOscPrefix(&(*currentPage), "/media/")) {
				float p = 1.f;
				backToCurrent(p);
				return;
			}
		}

		previousPage = currentPage;
		if (auto* target = findSubPageByNameAndPrefix(madOscQuery.subPages, subpageName, "/media/")) {
			MadParameterPage* prevPage = &(*currentPage);
			for (auto pageIt = madOscQuery.subPages.begin(); pageIt != madOscQuery.subPages.end(); ++pageIt) {
				if (&(*pageIt) == target) {
					currentPage = pageIt;
					setActivePage(&(*currentPage), prevPage);
					return;
				}
			}
		}

		oscSelectMedia(subpageName);
	}
}
void ofApp::showMedia(string& name) {
	ofRemoveListener(madOscQuery.mediaNameE, this, &ofApp::showMedia);

	if ((*currentPage).isSubpage()) return;
	previousPage = currentPage;

	std::list<MadParameterPage>::iterator pageIt;
	for (pageIt = madOscQuery.subPages.begin(); pageIt != madOscQuery.subPages.end(); pageIt++) {
		if (pageIt->getName() == name) {
			MadParameterPage* prevPage = &(*currentPage);
			currentPage = pageIt;
			setActivePage(&(*currentPage), prevPage);
			return;
		}
	}
	oscSelectMedia(name);
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

void ofApp::reload(float& p) {
	if (p == 1) {
		isLoading = true;
		auto success = reloadFromServer(p);
		madMapperLoadError = !success;
	} else
		isLoading = false;
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
	currentPage->unlinkDevice();
	if (auto c = ::getComponentByRole(dev, "nav.pageNext")) c->value.removeListener(this, &ofApp::pageForward);
	if (auto c = ::getComponentByRole(dev, "nav.pagePrev")) c->value.removeListener(this, &ofApp::pageBackward);
	if (auto c = ::getComponentByRole(dev, "nav.bankNext")) c->value.removeListener(this, &ofApp::bankForward);
	if (auto c = ::getComponentByRole(dev, "nav.bankPrev")) c->value.removeListener(this, &ofApp::bankBackward);
	if (auto c = ::getComponentByRole(dev, "nav.reload")) c->value.removeListener(this, &ofApp::updateValues);
	if (auto c = ::getComponentByRole(dev, "action.back")) c->value.removeListener(this, &ofApp::backToCurrent);

	if (dev->midiComponents.count("fader_M_video"))
		fadeMasterVideo->unlinkMidiComponent(dev->midiComponents["fader_M_video"]);
	if (dev->midiComponents.count("fader_M_dmx"))
		fadeMasterDMX->unlinkMidiComponent(dev->midiComponents["fader_M_dmx"]);
	if (dev->midiComponents.count("fader_Speed"))
		fadeEngingeSpeed->unlinkMidiComponent(dev->midiComponents["fader_Speed"]);
	if (dev->midiComponents.count("jog")) speed->unlinkMidiComponent(dev->midiComponents["jog"]);

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
	oscSendToServer(serverId, m);
}

void ofApp::oscSelectMedia(string name) {
	oscSelectMedia(name, 0);
}

void ofApp::oscSelectMedia(string name, size_t serverId) {
	string oscAddress = "/media/select_by_name";

	ofxOscMessage m;
	m.setAddress(oscAddress);
	m.addStringArg(name);
	oscSendToServer(serverId, m);
}

void ofApp::oscRequestMediaName() {
	oscRequestMediaName(0);
}

void ofApp::oscRequestMediaName(size_t serverId) {
	string oscAddress = "/getControlValues?url=/media/select_by_name";
	ofxOscMessage m;
	m.setAddress(oscAddress);
	m.addFloatArg(1);
	oscSendToServer(serverId, m);
}

//--------------------------------------------------------------
void ofApp::update() {
	if (!isLoading) {
		if (!madMapperLoadError) {
			ofSetWindowTitle("MADMAPPER MIDI MAPPER");
		} else {
			ofSetWindowTitle("MADMAPPER LOAD ERROR");
		}
	}

	oscParamSync.update();

	if (cueGridRefreshPending && !isLoading && !madMapperLoadError) {
		const uint64_t now = ofGetElapsedTimeMillis();
		if (now - lastCueGridRefreshMs >= 120) {
			madOscQuery.receive();
			rebuildCueGrid(madOscQuery.madMapperJson);
			cueGridRefreshPending = false;
			lastCueGridRefreshMs = now;
		}
	}

	// Throttled refresh so displays (e.g. Push3) show value changes even without page navigation.
	static uint64_t lastDisplayRefreshMs = 0;
	const uint64_t nowMs = ofGetElapsedTimeMillis();
	if (!isLoading && initialised && !madMapperLoadError && surface && currentPage != madOscQuery.pages.end()) {
		if (nowMs - lastDisplayRefreshMs >= 100) {
			updateParameterDisplay();
			updateSubpageMediaButtonFeedback();
			lastDisplayRefreshMs = nowMs;
		}
	}
}

void ofApp::setupPages(ofJson madmapperJson) {
	//    // Custom
	//    madOscQuery.createCustomPage(pages, &platformM,
	//    ofLoadJson("custom_page.json"));
	//
	//    // One for each possible Subpage
	//    madOscQuery.createSubPages(subPages, &platformM, madmapperJson);

	//    cout << "subPages " << subPages.size() << endl;

	// Set initial page
	currentPage = madOscQuery.pages.begin();
	previousPage = currentPage;

	setActivePage(&(*currentPage), nullptr);
}

static const ofJson* jsonGet(const ofJson& root, std::initializer_list<const char*> keys) {
	const ofJson* node = &root;
	for (auto k : keys) {
		if (!node->is_object()) return nullptr;
		auto it = node->find(k);
		if (it == node->end()) return nullptr;
		node = &(*it);
	}
	return node;
}

void ofApp::setupUI(ofJson madmapperJson) {
	if (!surface) return;
	auto* dev = static_cast<ofxMidiDevice*>(surface.get());
	rebuildCueGrid(madmapperJson);
	bindCueGrid();
	auto labelForRoleOrPrefix = [&](const std::string& role, const std::string& fallbackPrefix, int index) {
		auto bit = dev->bindings.find(role);
		if (bit != dev->bindings.end() && dev->midiComponents.count(bit->second)) return bit->second;
		std::string fallback = fallbackPrefix + ofToString(index);
		if (dev->midiComponents.count(fallback)) return fallback;
		return std::string();
	};

	if (auto c = ::getComponentByRole(dev, "nav.pageNext")) c->value.addListener(this, &ofApp::pageForward);
	if (auto c = ::getComponentByRole(dev, "nav.pagePrev")) c->value.addListener(this, &ofApp::pageBackward);
	if (auto c = ::getComponentByRole(dev, "nav.bankNext")) c->value.addListener(this, &ofApp::bankForward);
	if (auto c = ::getComponentByRole(dev, "nav.bankPrev")) c->value.addListener(this, &ofApp::bankBackward);
	if (auto c = ::getComponentByRole(dev, "nav.reload")) c->value.addListener(this, &ofApp::updateValues);
	if (auto c = ::getComponentByRole(dev, "action.back")) c->value.addListener(this, &ofApp::backToCurrent);

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
		fadeEngingeSpeed = madOscQuery.createParameter(*engineSpeed);
		fadeEngingeSpeed->linkMidiComponent(dev->midiComponents["fader_Speed"]);
	}
	const ofJson* bpm = jsonGet(madmapperJson, {"CONTENTS", "master", "CONTENTS", "Global_BPM", "CONTENTS", "BPM"});
	if (bpm && dev->midiComponents.count("jog")) {
		speed = madOscQuery.createParameter(*bpm);
		speed->linkMidiComponent(dev->midiComponents["jog"]);
	}

	selectGroup.doCheckbox = true;
	for (int i = 1; i < 17; i++) {
		std::string lbl = labelForRoleOrPrefix("param." + ofToString(i) + ".surfaceSubpage", "sel_", i);
		if (dev->midiComponents.count(lbl)) selectGroup.add(dev->midiComponents[lbl]);
	}
	ofAddListener(selectGroup.lastChangedE, this, &ofApp::selectSurface);

	muteGroup.doCheckbox = false;
	for (int i = 1; i < 17; i++) {
		std::string lbl = labelForRoleOrPrefix("param." + ofToString(i) + ".groupSubpage", "mute_", i);
		if (dev->midiComponents.count(lbl)) muteGroup.add(dev->midiComponents[lbl]);
	}
	ofAddListener(muteGroup.lastChangedE, this, &ofApp::selectSurface);
	ofAddListener(muteGroup.noneSelectedE, this, &ofApp::backToCurrent);

	soloGroup.doCheckbox = true;
	for (int i = 1; i < 17; i++) {
		std::string lbl = labelForRoleOrPrefix("param." + ofToString(i) + ".mediaSubpage", "solo_", i);
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

	ofSetWindowTitle(windowInfo.str());
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
	float p = 1; // for use in callback functions

	if (key == 's') {
		showStatusString = !showStatusString;

		//        platformM.saveMidiComponentsToFile("platformM.json");
	}
	if (key == 'm') {
		showMidiIn = !showMidiIn;
	}

	if (key == ' ') {
		//        auto success = reloadFromServer(p);
		//        madMapperLoadError = !success;
		madOscQuery.updateValues();
		rebuildCueGrid(madOscQuery.madMapperJson);
		updateParameterDisplay();
	}

	if (key == 'l') {
		// platformM.setupFromFile("platformM.json");
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
	if (prevPage != nullptr) {
		prevPage->unlinkDevice();
	}
	if(!noDeviceConnected) page->linkDevice();

	ofLog() << "Active page set to " << (*currentPage).getName() << endl;
	
	// call updateParameterDisplay() on initial page set
	updatePageDisplay();
	updateParameterDisplay();
	updateSubpageMediaButtonFeedback();
}

void ofApp::updateSubpageMediaButtonFeedback() {
	if (!surface) return;
	auto* dev = static_cast<ofxMidiDevice*>(surface.get());

	auto labelForRoleOrPrefix = [&](const std::string& role, const std::string& fallbackPrefix, int index) {
		auto bit = dev->bindings.find(role);
		if (bit != dev->bindings.end() && dev->midiComponents.count(bit->second)) return bit->second;
		std::string fallback = fallbackPrefix + ofToString(index);
		if (dev->midiComponents.count(fallback)) return fallback;
		return std::string();
	};

	auto extractSurfaceSubpageName = [](MadParameter* parameter) {
		if (!parameter || !parameter->isSelectable()) return std::string();
		auto oscAddress = ofSplitString(parameter->getOscAddress(), "/");
		int i = 0;
		while (i < (int)oscAddress.size() && oscAddress[i] != "opacity") i++;
		if (i <= 0 || i >= (int)oscAddress.size()) return std::string();
		return oscAddress[i - 1];
	};

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
		std::string surfaceSubpage = extractSurfaceSubpageName(parameter);
		std::string mediaSubpage = (parameter && parameter->isSelectable()) ? parameter->getConnectedMediaName() : std::string();

		std::string surfaceLabel = labelForRoleOrPrefix("param." + ofToString(i) + ".surfaceSubpage", "sel_", i);
		if (!surfaceLabel.empty()) {
			auto& c = dev->midiComponents[surfaceLabel];
			float v = (!surfaceSubpage.empty() && activeName == surfaceSubpage) ? 1.f : 0.f;
			c.value.disableEvents();
			c.value = v;
			c.update();
			c.value.enableEvents();
		}

		std::string mediaLabel = labelForRoleOrPrefix("param." + ofToString(i) + ".mediaSubpage", "solo_", i);
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

		// If we were initialised already, clear old bindings and pages first
		if (initialised) {
			removeListeners();
			selectGroup.clear();
			muteGroup.clear();
			soloGroup.clear();
			madOscQuery.pages.clear();
			madOscQuery.subPages.clear();
		}

		// Rebuild pages and UI
		const ofJson customPageJson = ofLoadJson("custom_page.json");
		madOscQuery.createCustomPages(static_cast<ofxMidiDevice*>(surface.get()), customPageJson,
									  madOscQuery.madMapperJson, 0);

		// Build pages for extra servers and splice into the main page list
		for (size_t i = 0; i < extraOscQueries.size(); ++i) {
			auto& extraServer = *extraOscQueries[i];
			extraServer.receive(); // refresh data from server
			if (extraServer.madMapperJson.is_null()) {
				ofLogWarning("ofApp") << "Extra server " << i + 1 << " unreachable, skipping page build";
				continue;
			}
			extraServer.pages.clear();
			extraServer.parameterMap.clear();
			extraServer.createCustomPages(static_cast<ofxMidiDevice*>(surface.get()), customPageJson,
										  extraServer.madMapperJson, i + 1);
			registerServerPathRouting(i + 1, extraServer);
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
				return true;
			}
		}
		ofLog(OF_LOG_NOTICE) << "Reload successful assigning to first page!" << endl;
		if (!madOscQuery.pages.empty()) {
			currentPage = madOscQuery.pages.begin();
			setActivePage(&(*currentPage), nullptr);
		}
		return true;
	}
	return false;
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
	ofLogNotice() << "\n\n### setupWebServer() CALLED - Setting up fetchers ###\n\n";

	// Bind API callbacks
	webServer->setPagesFetcher([this]() { return getPages(); });
	webServer->setPagesSaver([this](const ofJson& pages) { savePages(pages); });
	webServer->setParametersFetcher([this]() { 
				ofLogNotice() << "\n\n*** LAMBDA EXECUTED: getParameters() called from lambda ***\n\n";
		ofLogNotice() << "=== LAMBDA: getParameters() Lambda called ===";
		return getParameters(); 
	});
	webServer->setConfigFetcher([this]() { return getConfig(); });

	webServer->start();
}

ofJson ofApp::getPages() {
	// Load and return custom_page.json
	try {
		return ofLoadJson("custom_page.json");
	} catch (const std::exception& e) {
		ofLogError() << "Failed to load custom_page.json: " << e.what();
		return ofJson::object();
	}
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
		ofSaveJson("custom_page.json", validated);
		ofLogNotice() << "Saved custom_page.json with " << validated["pages"].size() << " pages";
	} catch (const std::exception& e) {
		ofLogError() << "Failed to save custom_page.json: " << e.what();
	} catch (...) {
		ofLogError() << "Unknown error saving custom_page.json";
	}
}

ofJson ofApp::getParameters() {
	// Return all parameters organized by server
	ofJson result = ofJson::object();
	std::vector<std::unordered_set<std::string>> serverOwnedPaths;
	serverOwnedPaths.resize(std::max<size_t>(1, oscServerConfigs.size()));
	for (const auto& kv : madOscQuery.parameterMap) {
		serverOwnedPaths[0].insert(kv.first);
	}
	for (size_t i = 0; i < extraOscQueries.size() && (i + 1) < serverOwnedPaths.size(); ++i) {
		if (!extraOscQueries[i]) continue;
		for (const auto& kv : extraOscQueries[i]->parameterMap) {
			serverOwnedPaths[i + 1].insert(kv.first);
		}
	}

	// Build server buckets first.
	for (size_t i = 0; i < oscServerConfigs.size(); ++i) {
		std::string serverId = "server_" + std::to_string(i);
		ofJson serverInfo = ofJson::object();
		serverInfo["name"] = oscServerConfigs[i].id;
		serverInfo["id"] = serverId;
		serverInfo["connected"] = i == 0
			? (madOscQuery.madMapperJson != nullptr)
			: ((i - 1) < extraOscQueries.size() && extraOscQueries[i - 1] != nullptr && extraOscQueries[i - 1]->madMapperJson != nullptr);
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

	auto appendParameter = [&](MadParameter* param) {
		if (param == nullptr) return;
		const std::string path = param->getOscAddress();
		size_t sid = 0;
		for (size_t s = 1; s < serverOwnedPaths.size(); ++s) {
			if (serverOwnedPaths[s].find(path) != serverOwnedPaths[s].end()) {
				sid = s;
				break;
			}
		}
		if (sid == 0) {
			sid = serverForOscPath(path);
		}
		const std::string serverId = "server_" + std::to_string(sid);
		if (!result.contains(serverId)) return;

		ofJson parameter = ofJson::object();
		parameter["name"] = param->getName();
		parameter["display"] = param->getDisplayParameterName();
		parameter["path"] = path;
		result[serverId]["parameters"].push_back(parameter);
	};

	for (auto& page : madOscQuery.pages) {
		const auto* params = page.getParameters();
		if (params == nullptr) continue;
		for (const auto& param : *params) {
			appendParameter(param);
		}
	}

	for (auto& subpage : madOscQuery.subPages) {
		const auto* params = subpage.getParameters();
		if (params == nullptr) continue;
		for (const auto& param : *params) {
			appendParameter(param);
		}
	}
	
	// Add current state info
	result["current_page"] = currentPage != madOscQuery.pages.end() ? currentPage->getName() : "";
	
	return result;
}

ofJson ofApp::getConfig() {
	// Return current configuration
	ofJson config = ofJson::object();

	config["servers"] = ofJson::array();
	for (const auto& serverConfig : oscServerConfigs) {
		ofJson sc = ofJson::object();
		sc["id"] = serverConfig.id;
		sc["ip"] = serverConfig.ip;
		sc["sendPort"] = serverConfig.sendPort;
		sc["feedbackPort"] = serverConfig.feedbackPort;
		sc["queryPort"] = serverConfig.queryPort;
		config["servers"].push_back(sc);
	}

	config["currentPage"] = currentPage != madOscQuery.pages.end() ? currentPage->getName() : "";
	config["initialized"] = initialised;
	config["noDeviceConnected"] = noDeviceConnected;

	if (activeProfile) {
		config["activeProfile"] = activeProfile->name;
	}

	return config;
}

// ============== WebServer Class Implementation ==============

#include "Poco/Net/HTTPServer.h"
#include "Poco/Net/HTTPRequestHandlerFactory.h"
#include "Poco/Net/HTTPRequestHandler.h"
#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/Net/HTTPServerResponse.h"
#include "Poco/Net/HTTPServerParams.h"
#include "Poco/Net/ServerSocket.h"
#include "Poco/Exception.h"
#include <sstream>

using Poco::Net::HTTPServer;
using Poco::Net::HTTPRequestHandler;
using Poco::Net::HTTPRequestHandlerFactory;
using Poco::Net::HTTPServerRequest;
using Poco::Net::HTTPServerResponse;
using Poco::Net::ServerSocket;

// API Request Handler
class APIRequestHandler : public HTTPRequestHandler {
  public:
	APIRequestHandler(WebServer* server) : webServer(server) {}

	void handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) override {
		response.setContentType("application/json");
		response.add("Access-Control-Allow-Origin", "*");
		response.add("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
		response.add("Access-Control-Allow-Headers", "Content-Type");

		std::string path = request.getURI();
		std::string method = request.getMethod();

		if (method == "OPTIONS") {
			response.setStatus(HTTPServerResponse::HTTP_OK);
			response.send();
			return;
		}

		if (path == "/api/pages" && method == "GET") {
			try {
				if (webServer->pagesFetcher) {
					ofJson pages = webServer->pagesFetcher();
					response.setStatus(HTTPServerResponse::HTTP_OK);
					std::string jsonStr = pages.dump();
					response.setContentLength(jsonStr.size());
					response.send() << jsonStr;
				} else {
					response.setStatus(HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR);
					response.send() << R"({"error":"pagesFetcher not set"})";
				}
			} catch (const std::exception& e) {
				ofLogError() << "Exception in /api/pages GET: " << e.what();
				response.setStatus(HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR);
				std::stringstream ss;
				ss << R"({"error":")" << e.what() << R"("})";
				response.send() << ss.str();
			}
		}
		else if (path == "/api/pages" && method == "POST") {
			std::istream& is = request.stream();
			std::stringstream buffer;
			buffer << is.rdbuf();
			std::string bodyStr = buffer.str();

			try {
				ofJson updated = ofJson::parse(bodyStr);
				if (webServer->pagesSaver) {
					webServer->pagesSaver(updated);
					response.setStatus(HTTPServerResponse::HTTP_OK);
					response.send() << R"({"status":"saved"})";
				} else {
					response.setStatus(HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR);
					response.send() << R"({"error":"pagesSaver not set"})";
				}
			} catch (const std::exception& e) {
				response.setStatus(HTTPServerResponse::HTTP_BAD_REQUEST);
				std::stringstream ss;
				ss << R"({"error":")" << e.what() << R"("})";
				response.send() << ss.str();
			}
		}
		else if (path == "/api/parameters" && method == "GET") {
			try {
				ofLogNotice() << "=== HANDLER: /api/parameters GET request received ===";
				if (webServer->parametersFetcher) {
					ofLogNotice() << "=== HANDLER: parametersFetcher is set, calling it ===";
					ofJson params = webServer->parametersFetcher();
					ofLogNotice() << "=== HANDLER: parametersFetcher returned ===";
					response.setStatus(HTTPServerResponse::HTTP_OK);
					std::string jsonStr = params.dump();
					response.setContentLength(jsonStr.size());
					response.send() << jsonStr;
				} else {
					ofLogError() << "=== HANDLER: parametersFetcher NOT set ===";
					response.setStatus(HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR);
					response.send() << R"({"error":"parametersFetcher not set"})";
				}
			} catch (const std::exception& e) {
				ofLogError() << "Exception in /api/parameters: " << e.what();
				response.setStatus(HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR);
				std::stringstream ss;
				ss << R"({"error":")" << e.what() << R"("})";
				response.send() << ss.str();
			}
		}
		else if (path == "/api/config" && method == "GET") {
			try {
				if (webServer->configFetcher) {
					ofJson config = webServer->configFetcher();
					response.setStatus(HTTPServerResponse::HTTP_OK);
					std::string jsonStr = config.dump();
					response.setContentLength(jsonStr.size());
					response.send() << jsonStr;
				} else {
					response.setStatus(HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR);
					response.send() << R"({"error":"configFetcher not set"})";
				}
			} catch (const std::exception& e) {
				ofLogError() << "Exception in /api/config: " << e.what();
				response.setStatus(HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR);
				std::stringstream ss;
				ss << R"({"error":")" << e.what() << R"("})";
				response.send() << ss.str();
			}
		}
		else {
			response.setStatus(HTTPServerResponse::HTTP_NOT_FOUND);
			response.send() << R"({"error":"not found"})";
		}
	}

  private:
	WebServer* webServer;
};

// Static File Handler
class StaticFileHandler : public HTTPRequestHandler {
  public:
	StaticFileHandler(const std::string& baseDir) : basePath(baseDir) {
		ofLogNotice() << "StaticFileHandler: basePath = " << basePath;
	}

	void handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) override {
		std::string resourcePath = request.getURI();
		if (resourcePath == "/") resourcePath = "/index.html";

		std::string filePath = basePath + resourcePath;
		ofLogNotice() << "StaticFileHandler: requesting " << resourcePath << " - trying: " << filePath;

		// Try to open the file
		std::ifstream file(filePath, std::ios::binary);
		if (file.good()) {
			ofLogNotice() << "StaticFileHandler: found file at " << filePath;
			response.setStatus(HTTPServerResponse::HTTP_OK);
			
			if (filePath.find(".js") != std::string::npos) {
				response.setContentType("application/javascript");
			} else if (filePath.find(".css") != std::string::npos) {
				response.setContentType("text/css");
			} else if (filePath.find(".json") != std::string::npos) {
				response.setContentType("application/json");
			} else {
				response.setContentType("text/html");
			}
			
			response.send() << file.rdbuf();
		} else {
			ofLogWarning() << "StaticFileHandler: file not found at " << filePath << ", trying index.html fallback";
			// Try index.html as fallback
			std::string indexPath = basePath + "/index.html";
			std::ifstream indexFile(indexPath, std::ios::binary);
			if (indexFile.good()) {
				ofLogNotice() << "StaticFileHandler: found index.html at " << indexPath;
				response.setStatus(HTTPServerResponse::HTTP_OK);
				response.setContentType("text/html");
				response.send() << indexFile.rdbuf();
			} else {
				ofLogError() << "StaticFileHandler: index.html not found at " << indexPath;
				response.setStatus(HTTPServerResponse::HTTP_NOT_FOUND);
				response.send() << "File not found";
			}
		}
	}

  private:
	std::string basePath;
};

// Request Handler Factory
class WebServerFactory : public HTTPRequestHandlerFactory {
  public:
	WebServerFactory(WebServer* server) : webServer(server) {
		// Use absolute path to web files - works regardless of working directory
		basePath = "/Users/jonasfehr/Documents/openFrameworks/apps/myApps/MadMapperControl_MM6_V2/bin/data/web";
		ofLogNotice() << "WebServerFactory: Using web directory: " << basePath;
	}

	HTTPRequestHandler* createRequestHandler(const HTTPServerRequest& request) override {
		std::string path = request.getURI();
		ofLogNotice() << "=== FACTORY: Request URI: '" << path << "' ===";
		if (path.find("/api/") == 0) {
			ofLogNotice() << "=== FACTORY: Routing to APIRequestHandler ===";
			return new APIRequestHandler(webServer);
		} else {
			ofLogNotice() << "=== FACTORY: Routing to StaticFileHandler ===";
			return new StaticFileHandler(basePath);
		}
	}

  private:
	WebServer* webServer;
	std::string basePath;
};

WebServer::WebServer(int port)
	: port(port), running(false), httpServer(nullptr) {}

WebServer::~WebServer() {
	stop();
}

void WebServer::start() {
	if (running) return;

	try {
		ofLogNotice() << "WebServer: Creating ServerSocket on port " << port;
		ServerSocket svs(port);
		
		ofLogNotice() << "WebServer: Creating request handler factory";
		WebServerFactory* factory = new WebServerFactory(this);
		
		ofLogNotice() << "WebServer: Creating HTTPServer instance";
		Poco::Net::HTTPServerParams* params = new Poco::Net::HTTPServerParams();
		params->setMaxQueued(100);
		params->setMaxThreads(4);
		
		httpServer = std::make_unique<HTTPServer>(factory, svs, params);
		
		ofLogNotice() << "WebServer: Starting HTTPServer";
		httpServer->start();
		
		running = true;
		ofLogNotice() << "WebServer started successfully on port " << port;
	} catch (const Poco::Exception& e) {
		ofLogError() << "Poco Exception in WebServer::start(): " << e.what();
		running = false;
	} catch (const std::exception& e) {
		ofLogError() << "std::exception in WebServer::start(): " << e.what();
		running = false;
	} catch (...) {
		ofLogError() << "Unknown exception in WebServer::start()";
		running = false;
	}
}

void WebServer::stop() {
	if (running && httpServer) {
		httpServer->stop();
		running = false;
		ofLogNotice() << "WebServer stopped";
	}
}

bool WebServer::isRunning() const {
	return running;
}

void WebServer::broadcastParameterUpdate(const std::string& path, float value, int serverId) {
	// TODO: Implement WebSocket broadcasting
}

void ofApp::updatePageDisplay() {
	if (!surface) return;
	if (currentPage == madOscQuery.pages.end()) return;
	surface->updatePageDisplay(currentPage->getName());
}

void ofApp::rebuildCueGrid(const ofJson& madMapperJson) {
	timelineGridState = TimelineGridState{};
	cueGridActive = false;
	availableCueBanks.clear();
	if (!surface || madMapperJson.is_null()) {
		if (surface) surface->updateTimelineGrid(timelineGridState);
		return;
	}

	int gridRows = 8;
	int gridCols = 8;
	bool flipTopOrigin = true;
	if (activeProfile && activeProfile->grid) {
		gridRows = activeProfile->grid->rows;
		gridCols = activeProfile->grid->cols;
		flipTopOrigin = activeProfile->grid->flipTopOrigin;
	}
	timelineGridState.rows = gridRows;
	timelineGridState.cols = gridCols;

	availableCueBanks = timelineBankNames(madMapperJson);
	cueBankName = resolveCueBankName(madMapperJson, availableCueBanks, cueBankName, cueFollowActiveBank);
	subscribeTimelinePaths();
	if (cueBankName.empty()) {
		surface->updateTimelineGrid(timelineGridState);
		return;
	}

	auto* bankContents = jsonGet(madMapperJson, {"CONTENTS", "timelines", "CONTENTS", cueBankName.c_str(), "CONTENTS"});
	auto* setupNode = bankContents ? jsonGet(*bankContents, {"setup"}) : nullptr;
	auto* byNameNode = bankContents ? jsonGet(*bankContents, {"by_name", "CONTENTS"}) : nullptr;
	if (!bankContents || !setupNode || !byNameNode) {
		surface->updateTimelineGrid(timelineGridState);
		return;
	}

	std::unordered_map<std::string, std::string> addressByName;
	for (auto it = byNameNode->begin(); it != byNameNode->end(); ++it) {
		if (!it.value().is_object()) continue;
		const ofJson* playFromBeginning = jsonGet(it.value(), {"CONTENTS", "play_from_beginning", "FULL_PATH"});
		const ofJson* play = jsonGet(it.value(), {"CONTENTS", "play", "FULL_PATH"});
		const ofJson* fullPath = jsonGet(it.value(), {"FULL_PATH"});
		if (playFromBeginning && playFromBeginning->is_string()) {
			addressByName[it.key()] = playFromBeginning->get<std::string>();
		} else if (play && play->is_string()) {
			addressByName[it.key()] = play->get<std::string>();
		} else if (fullPath && fullPath->is_string()) {
			addressByName[it.key()] = fullPath->get<std::string>() + "/play_from_beginning";
		}
	}

	ofJson setupJson;
	try {
		auto setupValue = firstValueJson(*setupNode);
		if (!setupValue) {
			surface->updateTimelineGrid(timelineGridState);
			return;
		}
		if (setupValue->is_string()) {
			auto setupJsonOpt = parseCueSetupValue(madMapperJson, bankContents, setupValue->get<std::string>());
			if (!setupJsonOpt) {
				surface->updateTimelineGrid(timelineGridState);
				return;
			}
			setupJson = *setupJsonOpt;
		} else if (setupValue->is_array() || setupValue->is_object()) {
			setupJson = *setupValue;
		} else {
			surface->updateTimelineGrid(timelineGridState);
			return;
		}
	} catch (const std::exception& exception) {
		ofLogWarning("ofApp") << "Failed to parse cue setup JSON: " << exception.what();
		surface->updateTimelineGrid(timelineGridState);
		return;
	}
	collectCueItems(setupJson,
					std::string(),
					addressByName,
					"/timelines/" + cueBankName + "/by_name",
					gridRows,
					flipTopOrigin,
					timelineGridState.cells);

	std::unordered_map<std::string, CueGridItem> deduped;
	for (const auto& cue : timelineGridState.cells) {
		if (!cue.isValid()) continue;
		deduped[ofToString(cue.row) + ":" + ofToString(cue.column)] = cue;
	}

	timelineGridState.cells.clear();
	for (const auto& entry : deduped) {
		timelineGridState.cells.push_back(entry.second);
	}

	std::sort(timelineGridState.cells.begin(), timelineGridState.cells.end(), [](const CueGridItem& left, const CueGridItem& right) {
		if (left.row != right.row) return left.row < right.row;
		return left.column < right.column;
	});

	timelineGridState.bankName = cueBankName;
	cueGridActive = !timelineGridState.empty();
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
	oscSendToServer(serverForOscPath(cue.oscAddress), message);
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

	surface->updateParameterDisplay(labels, values);
}


