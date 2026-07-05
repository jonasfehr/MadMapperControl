#pragma once

#include "ofxMadOscQuery.h"
#include "ofJson.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

inline constexpr int PORT_RECEIVE = 8010;
inline constexpr int PORT_FEEDBACK = 9893;

struct OscServerConfig {
	std::string id;
	std::string ip = "127.0.0.1";
	int sendPort = PORT_RECEIVE;
	int feedbackPort = PORT_FEEDBACK;
	int queryPort = PORT_RECEIVE;
	std::string discovery = "manual";

	// Parses settings.json ("servers" array, with legacy flat ip/port fallback).
	// Always returns at least one config.
	static std::vector<OscServerConfig> parseList(const ofJson& settings);
};

// Owns all OSCQuery server connections: the primary MadMapper instance plus
// any extra servers (e.g. TouchDesigner), endpoint health checks, Bonjour
// discovery and OSC-path→server routing.
//
// Locking: methods that are called from background threads or that mutate the
// server list lock internally. Accessors (server(), reachable(), sendTo(), …)
// are lock-free — callers that need consistency across several calls hold
// stateMutex(), exactly like the previous ofApp-owned oscStateMutex.
class OscServerManager {
  public:
	// Set configs without touching connections (startup).
	void setConfigs(std::vector<OscServerConfig> configs);
	// Replace configs at runtime: clears extras + routing, resets reachability,
	// disconnects and re-setups the primary connection. Locks internally.
	void applyConfigs(std::vector<OscServerConfig> configs);
	const std::vector<OscServerConfig>& configs() const { return configs_; }

	ofxMadOscQuery& primary() { return primary_; }
	// serverId 0 = primary, 1.. = extras; nullptr when out of range.
	ofxMadOscQuery* server(size_t serverId);
	const ofxMadOscQuery* server(size_t serverId) const;
	size_t extraCount() const { return extraQueries_.size(); }
	ofxMadOscQuery* extra(size_t index) { return index < extraQueries_.size() ? extraQueries_[index].get() : nullptr; }

	// primary().setup() from configs().front().
	void setupPrimary();
	// Full primary (re)connect: receive tree, websocket, subscribe, routing.
	bool connectPrimary();
	// Connect all extra servers (skips unreachable ones). Locks internally.
	void setupAdditionalServers();

	// Routing + sending
	void registerPathRouting(size_t serverId, const ofxMadOscQuery& server);
	size_t serverIdForPath(const std::string& oscPath) const; // 0 if unknown
	void sendTo(size_t serverId, ofxOscMessage& message);     // falls back to primary

	// Reachability / health
	bool reachable(size_t serverId) const;
	void refreshEndpointHealth(bool force); // blocking TCP probes; locks internally
	void pollHealthAsync();                 // throttled, probes on a background thread
	static bool endpointReachable(const OscServerConfig& cfg, std::string* error = nullptr);
	// Fired (from the probing thread) when the primary endpoint transitions
	// from unreachable to reachable.
	std::function<void()> onPrimaryBecameReachable;

	// Bonjour/mDNS discovery of _oscjson._tcp services (Linux/avahi only).
	void refreshBonjourServices(); // throttled, scans on a background thread
	std::vector<ofJson> bonjourSnapshot();

	std::mutex& stateMutex() { return oscStateMutex_; }

  private:
	ofxMadOscQuery primary_;
	std::vector<std::unique_ptr<ofxMadOscQuery>> extraQueries_;
	std::vector<OscServerConfig> configs_;
	std::vector<bool> reachability_;
	std::unordered_map<std::string, size_t> pathRouting_;
	std::mutex oscStateMutex_;

	uint64_t lastHealthCheckMs_ = 0;
	std::atomic_bool healthCheckInProgress_{false};

	std::vector<ofJson> bonjourDiscovered_;
	std::mutex bonjourMutex_;
	uint64_t lastBonjourScanMs_ = 0;
	std::atomic_bool bonjourScanInProgress_{false};
};
