#pragma once

#include "ofJson.h"
#include <functional>
#include <memory>

// Forward declarations
namespace Poco::Net {
	class HTTPServer;
}

// Simple WebServer interface - actual implementation in ofApp.cpp
class WebServer {
  public:
	using PagesFetcher = std::function<ofJson()>;
	using PagesSaver = std::function<void(const ofJson&)>;
	using PageActivator = std::function<void(const std::string&)>;
	using ParametersFetcher = std::function<ofJson()>;
	using ConfigFetcher = std::function<ofJson()>;
	using ConfigSaver = std::function<void(const ofJson&)>;

	WebServer(int port = 8080);
	~WebServer();

	void start();
	void stop();
	bool isRunning() const;

	void setPagesFetcher(PagesFetcher fetcher) { pagesFetcher = fetcher; }
	void setPagesSaver(PagesSaver saver) { pagesSaver = saver; }
	void setPageActivator(PageActivator activator) { pageActivator = activator; }
	void setParametersFetcher(ParametersFetcher fetcher) { parametersFetcher = fetcher; }
	void setConfigFetcher(ConfigFetcher fetcher) { configFetcher = fetcher; }
	void setConfigSaver(ConfigSaver saver) { configSaver = saver; }

	void broadcastParameterUpdate(const std::string& path, float value, int serverId);

	// Make callbacks accessible to implementation in ofApp
	PagesFetcher pagesFetcher;
	PagesSaver pagesSaver;
	PageActivator pageActivator;
	ParametersFetcher parametersFetcher;
	ConfigFetcher configFetcher;
	ConfigSaver configSaver;

  private:
	int port;
	bool running = false;
	std::unique_ptr<Poco::Net::HTTPServer> httpServer;
	
	friend class APIRequestHandler;
	friend class WebServerFactory;
};
