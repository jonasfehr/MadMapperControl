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
	using MappingsFetcher = std::function<ofJson()>;
	using MappingsSaver = std::function<void(const ofJson&)>;
	using ProfileFetcher = std::function<ofJson()>;
	using ProfileSaver = std::function<void(const ofJson&)>;
	using LearnStarter = std::function<void()>;
	using LearnStopper = std::function<void()>;
	using LearnStatusFetcher = std::function<ofJson()>;
	using LearnAssigner = std::function<void(const ofJson&)>;
	using LearnInjector = std::function<void(const ofJson&)>;

	WebServer(int port = 8080);
	~WebServer();

	void start();
	void stop();
	bool isRunning() const;

	void setPagesFetcher(PagesFetcher f)        { pagesFetcher = f; }
	void setPagesSaver(PagesSaver f)            { pagesSaver = f; }
	void setPageActivator(PageActivator f)      { pageActivator = f; }
	void setParametersFetcher(ParametersFetcher f) { parametersFetcher = f; }
	void setConfigFetcher(ConfigFetcher f)      { configFetcher = f; }
	void setConfigSaver(ConfigSaver f)          { configSaver = f; }
	void setMappingsFetcher(MappingsFetcher f)  { mappingsFetcher = f; }
	void setMappingsSaver(MappingsSaver f)      { mappingsSaver = f; }
	void setProfileFetcher(ProfileFetcher f)    { profileFetcher = f; }
	void setProfileSaver(ProfileSaver f)        { profileSaver = f; }
	void setLearnStarter(LearnStarter f)        { learnStarter = f; }
	void setLearnStopper(LearnStopper f)        { learnStopper = f; }
	void setLearnStatusFetcher(LearnStatusFetcher f) { learnStatusFetcher = f; }
	void setLearnAssigner(LearnAssigner f)      { learnAssigner = f; }
	void setLearnInjector(LearnInjector f)      { learnInjector = f; }

	void broadcastParameterUpdate(const std::string& path, float value, int serverId);

	PagesFetcher pagesFetcher;
	PagesSaver pagesSaver;
	PageActivator pageActivator;
	ParametersFetcher parametersFetcher;
	ConfigFetcher configFetcher;
	ConfigSaver configSaver;
	MappingsFetcher mappingsFetcher;
	MappingsSaver mappingsSaver;
	ProfileFetcher profileFetcher;
	ProfileSaver profileSaver;
	LearnStarter learnStarter;
	LearnStopper learnStopper;
	LearnStatusFetcher learnStatusFetcher;
	LearnAssigner learnAssigner;
	LearnInjector learnInjector;

  private:
	int port;
	bool running = false;
	std::unique_ptr<Poco::Net::HTTPServer> httpServer;
	
	friend class APIRequestHandler;
	friend class WebServerFactory;
};
