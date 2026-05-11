#pragma once

#include "DeviceProfile.h"
#include "Faderport16Surface.h"
#include "MidiControlSurface.h"
#include "PlatformMSurface.h"
#include "Push3Surface.h"
#include "WebServer.h"
#include "ofMain.h"
#include "ofxGui.h"
#include "ofxMadOscQuery.h"
#include "ofxMidiDevice.h"
#include "ofxOscParameterSync.h"
#include <memory>
#include <optional>
#include <unordered_map>

#define HOST "localhost"
#define PORT_RECEIVE 8010
#define PORT_FEEDBACK 9893

class writeLogToWindow : public ofBaseLoggerChannel {
  public:
	/// \brief Destroy the console logger channel.
	virtual ~writeLogToWindow() {};
	void log(ofLogLevel level, const std::string& module, const std::string& message);
};

class ofApp : public ofBaseApp {
  public:
	struct OscServerConfig {
		std::string id;
		std::string ip = "127.0.0.1";
		int sendPort = PORT_RECEIVE;
		int feedbackPort = PORT_FEEDBACK;
		int queryPort = PORT_RECEIVE;
	};

	void setup();
	void update();
	void draw();
	void exit();

	void keyPressed(int key);
	void keyReleased(int key);
	void mouseMoved(int x, int y);
	void mouseDragged(int x, int y, int button);
	void mousePressed(int x, int y, int button);
	void mouseReleased(int x, int y, int button);
	void mouseEntered(int x, int y);
	void mouseExited(int x, int y);
	void windowResized(int w, int h);
	void dragEvent(ofDragInfo dragInfo);
	void gotMessage(ofMessage msg);

	ofJson settings;
	string ip;
	int queryPort;
	int feedbackPort;
	int sendPort;
	ofxMadOscQuery madOscQuery;
	std::unique_ptr<MidiControlSurface> surface;
	std::optional<DeviceProfile> activeProfile;

	std::list<MadParameterPage>::iterator currentPage;
	std::list<MadParameterPage>::iterator previousPage;

	void setActivePage(MadParameterPage* page, MadParameterPage* prevPage);
	void setupPages(ofJson madMapperJson);
	void setupUI(ofJson madMapperJson);
	void rebuildCueGrid(const ofJson& madMapperJson);
	void bindCueGrid();
	void unbindCueGrid();
	void cycleCueBank(int direction);
	void drawStatusString();
	bool madMapperLoadError = true;
	bool initialised = false;
	ofImage errorImage;

	// FOR UX
	void updatePageDisplay();
	void updateParameterDisplay();
	void updateSubpageMediaButtonFeedback();
	void pageForward(float& p);
	void pageBackward(float& p);
	void bankForward(float& p);
	void bankBackward(float& p);
	MadParameter* fadeMasterVideo;
	MadParameter* fadeMasterDMX;
	MadParameter* fadeEngingeSpeed;
	MadParameter* speed;
	MidiComponentGroup selectGroup;
	MidiComponentGroup soloGroup;
	MidiComponentGroup muteGroup;
	void selectSurface(string& name);
	void selectGroupContent(string& name);
	void selectMedia(string& name);
	void showMedia(string& name);
	void triggerCue(const CueGridItem& cue);
	void onWebSocketPathUpdate(std::string& path);
	void subscribeTimelinePaths();
	void backToCurrent(float& p);
	bool reloadFromServer(float& p);
	void reload(float& p);
	void updateValues(float& p);

	bool isLoading;
	bool showMidiIn = false;
	bool showStatusString = false;
	bool noDeviceConnected = false; // true when no MIDI profile matches connected ports

	// OSC functions
	void oscSelectSurface(string name);
	void oscSelectSurface(string name, size_t serverId);
	void oscSelectMedia(string name);
	void oscSelectMedia(string name, size_t serverId);
	void oscRequestMediaName();
	void oscRequestMediaName(size_t serverId);

	void removeListeners();

	ofxOscParameterSync oscParamSync;
	ofxPanel gui;

	vector<MadParameter> madParameters;
	TimelineGridState timelineGridState;
	std::vector<std::string> availableCueBanks;
	std::string cueBankName = "Bank-1";
	bool cueFollowActiveBank = true;
	bool cueGridActive = false;
	bool cueGridRefreshPending = false;
	uint64_t lastCueGridRefreshMs = 0;

	MidiComponent* getComponentByRole(const std::string& role);

  private:
	ofxMadOscQuery* getOscServer(size_t serverId);
	const ofxMadOscQuery* getOscServer(size_t serverId) const;
	void setupAdditionalOscServers();
	void registerServerPathRouting(size_t serverId, const ofxMadOscQuery& server);
	void oscSendToServer(size_t serverId, ofxOscMessage& message);
	size_t serverForOscPath(const std::string& oscPath) const;

	// Web Server API methods
	void setupWebServer();
	ofJson getPages();
	void savePages(const ofJson& pages);
	ofJson getParameters();
	ofJson getConfig();

	std::vector<OscServerConfig> oscServerConfigs;
	std::vector<std::unique_ptr<ofxMadOscQuery>> extraOscQueries;
	std::unordered_map<std::string, size_t> oscPathServerRouting;
	std::unique_ptr<WebServer> webServer;
};
