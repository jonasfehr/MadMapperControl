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
	using ParametersFetcher = std::function<ofJson()>;
	using ConfigFetcher = std::function<ofJson()>;

	WebServer(int port = 8080);
	~WebServer();

	void start();
	void stop();
	bool isRunning() const;

	void setPagesFetcher(PagesFetcher fetcher) { pagesFetcher = fetcher; }
	void setPagesSaver(PagesSaver saver) { pagesSaver = saver; }
	void setParametersFetcher(ParametersFetcher fetcher) { parametersFetcher = fetcher; }
	void setConfigFetcher(ConfigFetcher fetcher) { configFetcher = fetcher; }

	void broadcastParameterUpdate(const std::string& path, float value, int serverId);

	// Make callbacks accessible to implementation in ofApp
	PagesFetcher pagesFetcher;
	PagesSaver pagesSaver;
	ParametersFetcher parametersFetcher;
	ConfigFetcher configFetcher;

  private:
	int port;
	bool running = false;
	std::unique_ptr<Poco::Net::HTTPServer> httpServer;
	
	friend class APIRequestHandler;
	friend class WebServerFactory;
};
