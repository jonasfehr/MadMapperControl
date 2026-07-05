#include "OscServerManager.h"
#include "ofMain.h"
#include "Poco/Net/SocketAddress.h"
#include "Poco/Net/StreamSocket.h"
#include "Poco/Timespan.h"
#include "Poco/Exception.h"
#include <algorithm>
#include <map>
#include <sstream>
#include <thread>

namespace {
	std::string safeStr(const ofJson& obj, const std::string& key, const std::string& def = "") {
		auto it = obj.find(key);
		if (it == obj.end() || !it->is_string()) return def;
		return it->get<std::string>();
	}

	int safeInt(const ofJson& obj, const std::string& key, int def = 0) {
		auto it = obj.find(key);
		if (it == obj.end() || !it->is_number()) return def;
		return it->get<int>();
	}
}

std::vector<OscServerConfig> OscServerConfig::parseList(const ofJson& settings) {
	std::vector<OscServerConfig> configs;

	if (settings.contains("servers") && settings["servers"].is_array()) {
		size_t index = 0;
		for (const auto& item : settings["servers"]) {
			if (!item.is_object()) continue;
			OscServerConfig cfg;
			cfg.id = safeStr(item, "id", "server_" + ofToString(index));
			cfg.ip = safeStr(item, "ip", "127.0.0.1");
			cfg.sendPort = safeInt(item, "sendPort", PORT_RECEIVE);
			cfg.feedbackPort = safeInt(item, "feedbackPort", PORT_FEEDBACK);
			cfg.queryPort = safeInt(item, "queryPort", cfg.sendPort);
			cfg.discovery = safeStr(item, "discovery", "manual");
			configs.push_back(cfg);
			++index;
		}
	}

	if (configs.empty()) {
		OscServerConfig legacy;
		legacy.id = "server_0";
		legacy.ip = safeStr(settings, "ip", "127.0.0.1");
		legacy.sendPort = safeInt(settings, "sendPort", PORT_RECEIVE);
		legacy.queryPort = safeInt(settings, "queryPort", legacy.sendPort);
		legacy.feedbackPort = safeInt(settings, "feedbackPort", PORT_FEEDBACK);
		legacy.discovery = "manual";
		configs.push_back(legacy);
	}

	return configs;
}

void OscServerManager::setConfigs(std::vector<OscServerConfig> configs) {
	configs_ = std::move(configs);
	reachability_.assign(configs_.size(), false);
}

void OscServerManager::applyConfigs(std::vector<OscServerConfig> configs) {
	std::lock_guard<std::mutex> lock(oscStateMutex_);
	configs_ = std::move(configs);
	reachability_.assign(configs_.size(), false);
	extraQueries_.clear();
	pathRouting_.clear();

	primary_.disconnectWebSocket();
	const auto& cfg = configs_.front();
	primary_.setup(cfg.ip, cfg.sendPort, cfg.feedbackPort, cfg.queryPort);
}

ofxMadOscQuery* OscServerManager::server(size_t serverId) {
	if (serverId == 0) return &primary_;
	const size_t extraIndex = serverId - 1;
	if (extraIndex >= extraQueries_.size()) return nullptr;
	return extraQueries_[extraIndex].get();
}

const ofxMadOscQuery* OscServerManager::server(size_t serverId) const {
	if (serverId == 0) return &primary_;
	const size_t extraIndex = serverId - 1;
	if (extraIndex >= extraQueries_.size()) return nullptr;
	return extraQueries_[extraIndex].get();
}

void OscServerManager::setupPrimary() {
	const auto& cfg = configs_.front();
	primary_.setup(cfg.ip, cfg.sendPort, cfg.feedbackPort, cfg.queryPort);
}

bool OscServerManager::connectPrimary() {
	primary_.receive();
	if (primary_.madMapperJson == nullptr) return false;
	if (!primary_.connectWebSocket(configs_.front().queryPort)) return false;
	primary_.subscribeAllParameters();
	registerPathRouting(0, primary_);
	return true;
}

void OscServerManager::setupAdditionalServers() {
	std::lock_guard<std::mutex> lock(oscStateMutex_);
	extraQueries_.clear();
	if (configs_.size() <= 1) return;

	for (size_t i = 1; i < configs_.size(); ++i) {
		const auto& cfg = configs_[i];
		if (i >= reachability_.size() || !reachability_[i]) {
			ofLogWarning("OscServerManager") << "Skipping unreachable OSCQuery server '" << cfg.id << "' at "
								 << cfg.ip << ":" << cfg.queryPort;
			// Don't call setup() for unreachable servers: it would trigger a blocking DNS lookup
			// and produce a spurious 'ofxOscSender: bad host?' error if hostname can't be resolved.
			extraQueries_.push_back(std::make_unique<ofxMadOscQuery>());
			continue;
		}
		auto server = std::make_unique<ofxMadOscQuery>();
		server->setup(cfg.ip, cfg.sendPort, cfg.feedbackPort, cfg.queryPort);
		server->receive();

		if (server->madMapperJson.is_null()) {
			ofLogWarning("OscServerManager") << "OSCQuery server '" << cfg.id << "' unreachable at "
									 << cfg.ip << ":" << cfg.queryPort;
			extraQueries_.push_back(std::move(server));
			continue;
		}

		if (server->connectWebSocket(cfg.queryPort)) {
			server->subscribeAllParameters();
			registerPathRouting(i, *server);
			ofLogNotice("OscServerManager") << "Additional OSCQuery server connected: " << cfg.id
									  << " (" << cfg.ip << ":" << cfg.queryPort << ")";
		} else {
			ofLogWarning("OscServerManager") << "Additional OSCQuery WebSocket failed: " << cfg.id
									 << " (" << cfg.ip << ":" << cfg.queryPort << ")";
		}

		extraQueries_.push_back(std::move(server));
	}
}

void OscServerManager::registerPathRouting(size_t serverId, const ofxMadOscQuery& server) {
	for (const auto& kv : server.parameterMap) {
		pathRouting_.emplace(kv.first, serverId);
	}
}

size_t OscServerManager::serverIdForPath(const std::string& oscPath) const {
	auto it = pathRouting_.find(oscPath);
	if (it != pathRouting_.end()) return it->second;
	return 0;
}

void OscServerManager::sendTo(size_t serverId, ofxOscMessage& message) {
	if (auto* target = server(serverId)) {
		target->oscSendToMadMapper(message);
		return;
	}
	primary_.oscSendToMadMapper(message);
}

bool OscServerManager::reachable(size_t serverId) const {
	return serverId < reachability_.size() ? reachability_[serverId] : false;
}

bool OscServerManager::endpointReachable(const OscServerConfig& cfg, std::string* error) {
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

void OscServerManager::pollHealthAsync() {
	// Health check runs async to avoid blocking the main thread on slow DNS
	// lookups (e.g. madmapper.local via mDNS). Time-guard before spawning so we
	// don't create threads at 60fps when idle.
	const uint64_t nowMs = ofGetElapsedTimeMillis();
	if (healthCheckInProgress_.load() || (nowMs - lastHealthCheckMs_) < 3000) return;
	healthCheckInProgress_.store(true);
	std::thread([this]() {
		refreshEndpointHealth(false);
		healthCheckInProgress_.store(false);
	}).detach();
}

void OscServerManager::refreshEndpointHealth(bool force) {
	const uint64_t nowMs = ofGetElapsedTimeMillis();
	if (!force && (nowMs - lastHealthCheckMs_) < 3000) return;
	lastHealthCheckMs_ = nowMs;

	std::vector<OscServerConfig> configsSnapshot;
	{
		std::lock_guard<std::mutex> lock(oscStateMutex_);
		configsSnapshot = configs_;
		if (reachability_.size() < configsSnapshot.size()) {
			reachability_.resize(configsSnapshot.size(), false);
		}
	}
	if (configsSnapshot.empty()) return;
	std::vector<bool> probed(configsSnapshot.size(), false);
	std::vector<std::string> errors(configsSnapshot.size());

	for (size_t i = 0; i < configsSnapshot.size(); ++i) {
		probed[i] = endpointReachable(configsSnapshot[i], &errors[i]);
	}

	bool primaryBecameReachable = false;
	std::vector<std::string> becameReachable;
	std::vector<std::string> becameUnreachable;
	{
		std::lock_guard<std::mutex> lock(oscStateMutex_);
		for (size_t i = 0; i < configsSnapshot.size(); ++i) {
			const bool isReachable = probed[i];
			const bool oldValue = reachability_[i];
			reachability_[i] = isReachable;

			if (isReachable != oldValue) {
				if (isReachable) {
					becameReachable.push_back(configsSnapshot[i].id + " (" + configsSnapshot[i].ip + ":" + ofToString(configsSnapshot[i].queryPort) + ")");
					if (i == 0) primaryBecameReachable = true;
				} else {
					becameUnreachable.push_back(configsSnapshot[i].id + " (" + configsSnapshot[i].ip + ":" + ofToString(configsSnapshot[i].queryPort) + ") reason=" + errors[i]);
				}
			}
		}
	}

	for (const auto& msg : becameReachable) {
		ofLogNotice("OscServerManager") << "Endpoint reachable again: " << msg;
	}
	for (const auto& msg : becameUnreachable) {
		ofLogWarning("OscServerManager") << "Endpoint unreachable: " << msg;
	}

	if (primaryBecameReachable && onPrimaryBecameReachable) {
		onPrimaryBecameReachable();
	}
}

void OscServerManager::refreshBonjourServices() {
	const uint64_t nowMs = ofGetElapsedTimeMillis();
	if (bonjourScanInProgress_.load() || (nowMs - lastBonjourScanMs_) < 15000) return;
	lastBonjourScanMs_ = nowMs;
	bonjourScanInProgress_.store(true);

	std::thread([this]() {
		std::vector<ofJson> found;
#ifdef TARGET_LINUX
		FILE* pipe = popen("avahi-browse -t -r -p _oscjson._tcp 2>/dev/null", "r");
		if (pipe) {
			char buf[512];
			std::map<std::string, ofJson> byKey;
			while (fgets(buf, sizeof(buf), pipe)) {
				std::string line(buf);
				if (line.empty() || line[0] != '=') continue;
				// format: =;iface;proto;name;type;domain;hostname;address;port;txt
				std::vector<std::string> parts;
				std::stringstream ss(line);
				std::string tok;
				while (std::getline(ss, tok, ';')) parts.push_back(tok);
				if (parts.size() < 9) continue;
				std::string name = parts[3];
				std::string ip   = parts[7];
				int port = 0;
				try { port = std::stoi(parts[8]); } catch (...) { continue; }
				if (ip.empty() || port <= 0) continue;
				// Strip ":port" suffix from name if present (e.g. "MadMapper:9001" → "MadMapper")
				std::string id = name;
				auto colon = id.rfind(':');
				if (colon != std::string::npos) {
					std::string suf = id.substr(colon + 1);
					if (!suf.empty() && std::all_of(suf.begin(), suf.end(), ::isdigit))
						id = id.substr(0, colon);
				}
				ofJson svc;
				svc["id"] = id;
				svc["ip"] = ip;
				svc["queryPort"] = port;
				svc["sendPort"]  = port;
				svc["feedbackPort"] = PORT_FEEDBACK;
				byKey[name + ip] = svc;
			}
			pclose(pipe);
			for (auto& kv : byKey) found.push_back(kv.second);
		}
#endif
		{
			std::lock_guard<std::mutex> lock(bonjourMutex_);
			bonjourDiscovered_ = std::move(found);
		}
		bonjourScanInProgress_.store(false);
	}).detach();
}

std::vector<ofJson> OscServerManager::bonjourSnapshot() {
	std::lock_guard<std::mutex> lock(bonjourMutex_);
	return bonjourDiscovered_;
}
