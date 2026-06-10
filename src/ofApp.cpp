#include "ofApp.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include "Poco/Net/SocketAddress.h"
#include "Poco/Net/StreamSocket.h"
#include "Poco/Timespan.h"
#include "Poco/Exception.h"

static const ofJson* jsonGet(const ofJson& root, std::initializer_list<const char*> keys);

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
			const std::string ip = item.value("ip", std::string());
			const int query = item.value("queryPort", PORT_RECEIVE);
			if (ip.empty() || query <= 0 || query > 65535) continue;

			const std::string dedupeKey = ofToLower(ip) + ":" + ofToString(query);
			if (seenHostPort.count(dedupeKey)) continue;
			seenHostPort.insert(dedupeKey);

			ofJson out = ofJson::object();
			out["id"] = item.value("id", "server_" + ofToString(index));
			out["ip"] = ip;
			out["queryPort"] = query;
			out["sendPort"] = item.value("sendPort", query);
			out["feedbackPort"] = item.value("feedbackPort", PORT_FEEDBACK);
			out["discovery"] = item.value("discovery", std::string("manual"));
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

	bool isBindableOscType(const ofJson& node) {
		auto itType = node.find("TYPE");
		if (itType == node.end() || !itType->is_string()) return false;
		const std::string type = itType->get<std::string>();
		return type == "f" || type == "d" || type == "i" || type == "h" || type == "T" || type == "F";
	}

	std::string oscNodeDisplayName(const ofJson& node, const std::string& path) {
		auto itDescription = node.find("DESCRIPTION");
		if (itDescription != node.end() && itDescription->is_string()) {
			const auto description = itDescription->get<std::string>();
			if (!description.empty()) return description;
		}
		auto tokens = ofSplitString(path, "/", true, true);
		return tokens.empty() ? path : tokens.back();
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
				cfg.discovery = item.value("discovery", std::string("manual"));
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
			legacy.discovery = "manual";
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
	endpointReachability.assign(oscServerConfigs.size(), false);
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
			bool inMatch = midiPortMatches(inPorts, p.midiInPort);
			bool outMatch = midiPortMatches(outPorts, p.midiOutPort);
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
	currentPage = madOscQuery.pages.end();
	previousPage = madOscQuery.pages.end();
	{
		std::lock_guard<std::mutex> lock(activePageMutex);
		activePageName.clear();
	}
	gui.setup();
	refreshEndpointHealth(true);

	if (endpointReachability.empty() || endpointReachability[0]) {
		madOscQuery.receive();
		ofSleepMillis(100);
	}
	if (endpointReachability.empty() || !endpointReachability[0] || madOscQuery.madMapperJson == nullptr) {
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
	std::lock_guard<std::mutex> lock(oscStateMutex);
	extraOscQueries.clear();
	if (oscServerConfigs.size() <= 1) return;

	for (size_t i = 1; i < oscServerConfigs.size(); ++i) {
		const auto& cfg = oscServerConfigs[i];
		if (i >= endpointReachability.size() || !endpointReachability[i]) {
			ofLogWarning("ofApp") << "Skipping unreachable OSCQuery server '" << cfg.id << "' at "
								 << cfg.ip << ":" << cfg.queryPort;
			// Don't call setup() for unreachable servers: it would trigger a blocking DNS lookup
			// and produce a spurious 'ofxOscSender: bad host?' error if hostname can't be resolved.
			extraOscQueries.push_back(std::make_unique<ofxMadOscQuery>());
			continue;
		}
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
	
	if (currentPage == madOscQuery.pages.end()) return;

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
	if (currentPage == madOscQuery.pages.end()) return;
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
		fadeEngineSpeed->unlinkMidiComponent(dev->midiComponents["fader_Speed"]);
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
	applyPendingServerConfig();
	// Health check runs async to avoid blocking main thread on slow DNS lookups (e.g. madmapper.local via mDNS).
	// Guard with a time check before spawning so we don't create threads at 60fps when idle.
	{
		const uint64_t nowCheck = ofGetElapsedTimeMillis();
		if (!healthCheckInProgress.load() && (nowCheck - lastEndpointHealthCheckMs >= 3000)) {
			healthCheckInProgress.store(true);
			std::thread([this]() {
				refreshEndpointHealth(false);
				healthCheckInProgress.store(false);
			}).detach();
		}
	}

	if (hasPendingReload.exchange(false)) {
		reloadRequested = true;
	}

	// MIDI hot-plug: scan for port changes every 2 seconds
	{
		const uint64_t nowMidi = ofGetElapsedTimeMillis();
		if (initialised && (nowMidi - lastMidiScanMs >= 2000)) {
			lastMidiScanMs = nowMidi;
			const auto currentInPorts = ofxMidiIn().getInPortList();
			if (currentInPorts != lastKnownInPorts) {
				lastKnownInPorts = currentInPorts;
				if (noDeviceConnected) {
					tryConnectMidiDevice();
				} else if (activeProfile) {
					bool stillPresent = midiPortMatches(currentInPorts, activeProfile->midiInPort);
					if (!stillPresent) disconnectMidiDevice();
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
		fadeEngineSpeed = madOscQuery.createParameter(*engineSpeed);
		fadeEngineSpeed->linkMidiComponent(dev->midiComponents["fader_Speed"]);
	}
	const ofJson* bpm = jsonGet(madmapperJson, {"CONTENTS", "master", "CONTENTS", "Global_BPM", "CONTENTS", "BPM"});
	if (bpm && dev->midiComponents.count("jog")) {
		speed = madOscQuery.createParameter(*bpm);
		speed->linkMidiComponent(dev->midiComponents["jog"]);
	}

	selectGroup.doCheckbox = true;
	for (int i = 1; i < 17; i++) {
		std::string lbl = labelForRoleOrPrefix(dev, "param." + ofToString(i) + ".surfaceSubpage", "sel_", i);
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

		std::string surfaceLabel = labelForRoleOrPrefix(dev, "param." + ofToString(i) + ".surfaceSubpage", "sel_", i);
		if (!surfaceLabel.empty()) {
			auto& c = dev->midiComponents[surfaceLabel];
			float v = (!surfaceSubpage.empty() && activeName == surfaceSubpage) ? 1.f : 0.f;
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
		std::lock_guard<std::mutex> lock(oscStateMutex);
		if (!endpointReachability.empty() && !endpointReachability[0]) {
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

		// If we were initialised already, clear old bindings and pages first
		if (initialised) {
			removeListeners();
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
		}

		// Rebuild pages and UI
		const ofJson customPageJson = ofLoadJson("custom_page.json");
		madOscQuery.createCustomPages(static_cast<ofxMidiDevice*>(surface.get()), customPageJson,
									  madOscQuery.madMapperJson, 0);

		// Build pages for extra servers and splice into the main page list
		for (size_t i = 0; i < extraOscQueries.size(); ++i) {
			if (i + 1 >= endpointReachability.size() || !endpointReachability[i + 1]) {
				ofLogWarning("ofApp") << "Extra server " << i + 1 << " unreachable, skipping page build";
				continue;
			}
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
	surface.reset();
	activeProfile.reset();
	noDeviceConnected = true;
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
	if (!found) return;

	activeProfile = found;
	ofLogNotice("ofApp") << "MIDI device connected: " << activeProfile->name;

	if (activeProfile->name.find("Push") != std::string::npos)
		surface = std::make_unique<Push3Surface>();
	else if (activeProfile->name.find("Platform") != std::string::npos)
		surface = std::make_unique<PlatformMSurface>();
	else
		surface = std::make_unique<Faderport16Surface>();

	static_cast<ofxMidiDevice*>(surface.get())->setupFromProfile(*activeProfile);
	surface->onProfileLoaded(*activeProfile);
	noDeviceConnected = false;

	// Re-bind to current page and re-wire all listeners if already running
	if (initialised && !madOscQuery.madMapperJson.is_null()
	    && currentPage != madOscQuery.pages.end()) {
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
	webServer->setPagesFetcher([this]() { return getPages(); });
	webServer->setPagesSaver([this](const ofJson& pages) { savePages(pages); });
	webServer->setPageActivator([this](const std::string& pageName) { requestActivatePageByName(pageName); });
	webServer->setParametersFetcher([this]() { return getParameters(); });
	webServer->setConfigFetcher([this]() { return getConfig(); });
	webServer->setConfigSaver([this](const ofJson& config) { saveConfig(config); });

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
	std::lock_guard<std::mutex> lock(oscStateMutex);
	// Return all parameters organized by server
	ofJson result = ofJson::object();
	std::vector<std::unordered_set<std::string>> emittedPaths;
	emittedPaths.resize(std::max<size_t>(1, oscServerConfigs.size()));

	// Build server buckets first.
	for (size_t i = 0; i < oscServerConfigs.size(); ++i) {
		std::string serverId = "server_" + std::to_string(i);
		ofJson serverInfo = ofJson::object();
		serverInfo["name"] = oscServerConfigs[i].id;
		serverInfo["id"] = serverId;
		const bool reachable = i < endpointReachability.size() ? endpointReachability[i] : false;
		serverInfo["connected"] = reachable && (i == 0
			? (madOscQuery.madMapperJson != nullptr)
			: ((i - 1) < extraOscQueries.size() && extraOscQueries[i - 1] != nullptr && extraOscQueries[i - 1]->madMapperJson != nullptr));
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

	if ((endpointReachability.empty() || endpointReachability[0]) && madOscQuery.madMapperJson.is_object()) {
		collectParameters(collectParameters, 0, madOscQuery.madMapperJson);
	}

	for (size_t i = 0; i < extraOscQueries.size(); ++i) {
		if (i + 1 < endpointReachability.size() && !endpointReachability[i + 1]) continue;
		if (!extraOscQueries[i] || !extraOscQueries[i]->madMapperJson.is_object()) continue;
		collectParameters(collectParameters, i + 1, extraOscQueries[i]->madMapperJson);
	}
	
	// Add current state info without touching potentially invalid iterators.
	{
		std::lock_guard<std::mutex> pageLock(activePageMutex);
		result["current_page"] = activePageName;
	}
	
	return result;
}

ofJson ofApp::getConfig() {
	std::lock_guard<std::mutex> lock(oscStateMutex);
	// Return current configuration
	ofJson config = ofJson::object();

	config["servers"] = ofJson::array();
	config["bonjourAnnouncements"] = ofJson::array();
	for (size_t i = 0; i < oscServerConfigs.size(); ++i) {
		const auto& serverConfig = oscServerConfigs[i];
		ofJson sc = ofJson::object();
		sc["id"] = serverConfig.id;
		sc["ip"] = serverConfig.ip;
		sc["sendPort"] = serverConfig.sendPort;
		sc["feedbackPort"] = serverConfig.feedbackPort;
		sc["queryPort"] = serverConfig.queryPort;
		sc["discovery"] = serverConfig.discovery;
		sc["reachable"] = i < endpointReachability.size() ? endpointReachability[i] : false;
		config["servers"].push_back(sc);
		if (serverConfig.discovery == "bonjour") {
			ofJson announced = ofJson::object();
			announced["id"] = serverConfig.id;
			announced["ip"] = serverConfig.ip;
			announced["queryPort"] = serverConfig.queryPort;
			announced["sendPort"] = serverConfig.sendPort;
			announced["feedbackPort"] = serverConfig.feedbackPort;
			announced["reachable"] = i < endpointReachability.size() ? endpointReachability[i] : false;
			config["bonjourAnnouncements"].push_back(announced);
		}
	}

	{
		std::lock_guard<std::mutex> pageLock(activePageMutex);
		config["currentPage"] = activePageName;
	}
	config["initialized"] = initialised;
	config["noDeviceConnected"] = noDeviceConnected;

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

	std::vector<OscServerConfig> newConfigs = parseOscServerConfigs(settings);
	if (newConfigs.empty()) {
		ofLogError("ofApp") << "applyPendingServerConfig: no valid servers";
		return;
	}

	{
		std::lock_guard<std::mutex> lock(oscStateMutex);
		oscServerConfigs = std::move(newConfigs);
		endpointReachability.assign(oscServerConfigs.size(), false);
		extraOscQueries.clear();
		oscPathServerRouting.clear();

		ip = oscServerConfigs.front().ip;
		sendPort = oscServerConfigs.front().sendPort;
		queryPort = oscServerConfigs.front().queryPort;
		feedbackPort = oscServerConfigs.front().feedbackPort;

		madOscQuery.disconnectWebSocket();
		madOscQuery.setup(ip, sendPort, feedbackPort, queryPort);
	}

	refreshEndpointHealth(true);
	bool primaryReachable = false;
	{
		std::lock_guard<std::mutex> lock(oscStateMutex);
		primaryReachable = !endpointReachability.empty() && endpointReachability[0];
	}
	if (primaryReachable) {
		std::lock_guard<std::mutex> lock(oscStateMutex);
		madOscQuery.receive();
		if (madOscQuery.connectWebSocket(queryPort)) {
			madOscQuery.subscribeAllParameters();
			registerServerPathRouting(0, madOscQuery);
			subscribeTimelinePaths();
		}
	}

	setupAdditionalOscServers();
	hasPendingReload.store(true);
	requestActivatePageByName(configToApply.value("currentPage", std::string()));
}

bool ofApp::endpointReachable(const OscServerConfig& cfg, std::string* error) const {
	try {
		Poco::Net::SocketAddress address(cfg.ip, cfg.queryPort);
		Poco::Net::StreamSocket socket;
		Poco::Timespan timeout(0, 0, 0, 0, 350000);
		socket.connect(address, timeout);
		socket.close();
		return true;
	} catch (const Poco::Exception& e) {
		if (error) *error = e.displayText();
		return false;
	} catch (const std::exception& e) {
		if (error) *error = e.what();
		return false;
	} catch (...) {
		if (error) *error = "unknown error";
		return false;
	}
}

void ofApp::refreshEndpointHealth(bool force) {
	const uint64_t nowMs = ofGetElapsedTimeMillis();
	if (!force && (nowMs - lastEndpointHealthCheckMs) < 3000) return;
	lastEndpointHealthCheckMs = nowMs;

	std::vector<OscServerConfig> configsSnapshot;
	{
		std::lock_guard<std::mutex> lock(oscStateMutex);
		configsSnapshot = oscServerConfigs;
		if (endpointReachability.size() < configsSnapshot.size()) {
			endpointReachability.resize(configsSnapshot.size(), false);
		}
	}
	if (configsSnapshot.empty()) return;
	std::vector<bool> probed(configsSnapshot.size(), false);
	std::vector<std::string> errors(configsSnapshot.size());

	for (size_t i = 0; i < configsSnapshot.size(); ++i) {
		probed[i] = endpointReachable(configsSnapshot[i], &errors[i]);
	}

	std::vector<std::string> becameReachable;
	std::vector<std::string> becameUnreachable;
	{
		std::lock_guard<std::mutex> lock(oscStateMutex);
		for (size_t i = 0; i < configsSnapshot.size(); ++i) {
			const bool reachable = probed[i];
			const bool oldValue = endpointReachability[i];
			endpointReachability[i] = reachable;

			if (reachable != oldValue) {
				if (reachable) {
					becameReachable.push_back(configsSnapshot[i].id + " (" + configsSnapshot[i].ip + ":" + ofToString(configsSnapshot[i].queryPort) + ")");
				} else {
					becameUnreachable.push_back(configsSnapshot[i].id + " (" + configsSnapshot[i].ip + ":" + ofToString(configsSnapshot[i].queryPort) + ") reason=" + errors[i]);
				}
			}
		}
	}

	for (const auto& msg : becameReachable) {
		ofLogNotice("ofApp") << "Endpoint reachable again: " << msg;
	}
	for (const auto& msg : becameUnreachable) {
		ofLogWarning("ofApp") << "Endpoint unreachable: " << msg;
	}

}


void ofApp::updatePageDisplay() {
	if (!surface) return;
	if (currentPage == madOscQuery.pages.end()) return;
	const std::string pageName = currentPage->getName();
	if (pageName.empty()) return;
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


