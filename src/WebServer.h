#pragma once

#include "ofJson.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

// Forward declarations
namespace Poco::Net {
	class HTTPServer;
	class WebSocket;
}

// REST API + static file server for the web config UI.
// Callbacks are plain public members — assign them directly before start().
class WebServer {
  public:
	using JsonFetcher = std::function<ofJson()>;
	using JsonHandler = std::function<void(const ofJson&)>;
	using PageActivator = std::function<void(const std::string&)>;
	using Action = std::function<void()>;

	WebServer(int port = 8080);
	~WebServer();

	void start();
	void stop();

	// ── WebSocket push (app → browsers, endpoint /ws) ─────────────────────────
	// Thread-safe; a slow/broken client is dropped rather than blocking.
	void broadcast(const std::string& message);
	bool hasClients();
	// Bumped on every client connect — lets the app force a full state resend.
	uint64_t clientGeneration() const { return wsGeneration.load(); }

	JsonFetcher pagesFetcher;
	JsonHandler pagesSaver;
	PageActivator pageActivator;
	JsonFetcher parametersFetcher;
	JsonFetcher configFetcher;
	JsonHandler configSaver;
	JsonFetcher mappingsFetcher;
	JsonHandler mappingsSaver;
	JsonFetcher profileFetcher;
	JsonFetcher profilesFetcher; // all profiles
	JsonHandler profileSaver;
	Action learnStarter;
	Action learnStopper;
	JsonFetcher learnStatusFetcher;
	JsonHandler learnAssigner;
	JsonHandler learnInjector;
	JsonFetcher displayFetcher;        // live device display state for the emulator
	JsonHandler midiInjector;          // raw MIDI from the emulator into the surface
	JsonHandler emulatorSurfaceSetter; // switch which profile the virtual surface emulates

  private:
	int port;
	std::atomic<bool> running{false};

	std::vector<std::shared_ptr<Poco::Net::WebSocket>> wsClients;
	std::mutex wsMutex;
	std::atomic<uint64_t> wsGeneration{0};
	// Declared last: its destructor joins the handler threads, which still
	// touch wsClients/wsMutex — those must outlive it.
	std::unique_ptr<Poco::Net::HTTPServer> httpServer;
	friend class WSRequestHandler;
};
