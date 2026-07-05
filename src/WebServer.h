#pragma once

#include "ofJson.h"
#include <functional>
#include <memory>

// Forward declarations
namespace Poco::Net {
	class HTTPServer;
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

  private:
	int port;
	bool running = false;
	std::unique_ptr<Poco::Net::HTTPServer> httpServer;
};
