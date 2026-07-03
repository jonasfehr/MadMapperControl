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
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <atomic>
#include <thread>
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
		std::string discovery = "manual";
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
	bool activatePageByName(const std::string& pageName);
	void requestActivatePageByName(const std::string& pageName);
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
	MadParameter* fadeMasterVideo = nullptr;
	MadParameter* fadeMasterDMX   = nullptr;
	MadParameter* fadeEngineSpeed  = nullptr;
	MadParameter* speed            = nullptr;
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
	std::atomic<bool> cueGridRefreshPending{false};
	uint64_t lastCueGridRefreshMs = 0;

	MidiComponent* getComponentByRole(const std::string& role);

	// TD hover encoder — Push3 master encoder routes relative MIDI to the
	// currently hovered TouchDesigner parameter via OSC.
	void onTdHoverEncoderChange(float& v);
	float    tdHoverEncoderPrevValue = 0.f;
	uint64_t tdHoverEncoderLastMs   = 0;
	std::string tdHoverEncoderOscPath = "/ParHoverMIDI_VSN1/knob_delta";
	static constexpr float kHoverAccelBase = 8.f;
	static constexpr float kHoverAccelMax  = 12.f;
	size_t tdServerId = SIZE_MAX;

	// ── Mappings (loaded from bin/data/mappings.json) ────────────────────────
	struct FixedMapping {
		std::string path;
		size_t serverId = 0;
		std::string mode = "absolute"; // "absolute" | "delta"
	};
	struct PageGotoEntry {
		int index = 0;
		std::string role;     // e.g. "nav.pageGoto.0"
		std::string pageName; // target page name
	};

	// Per-shortcut listener so OF's typed addListener can capture the page name.
	// Stores the role so removeListeners can look up the component for cleanup.
	struct PageGotoListener {
		std::string role;
		std::string pageName;
		ofApp*      app = nullptr;
		void onPress(float& v) { if (v > 0.f && app) app->requestActivatePageByName(pageName); }
	};

	// Delta-mode fixed binding: polled in update(), sends raw OSC delta
	struct ActiveFixedBinding {
		MidiComponent* component;
		FixedMapping   mapping;
		float          prevValue = 0.f;
		uint64_t       lastMs    = 0;
	};

	// Absolute-mode fixed binding: linked via MadParameter (acceleration built-in)
	struct LinkedFixedParam {
		MidiComponent* component;
		MadParameter*  param;
	};

	// ── Delta-mode encoder acceleration ────────────────────────────────────────
	// Acceleration for fixed delta-mode encoders (absolute-mode accel is in MadParameter).
	static constexpr float kFixedAccelBase = 6.f;
	static constexpr float kFixedAccelMax  = 8.f;

	ofJson mappingsJson;
	std::unordered_map<std::string, FixedMapping> fixedMappings;
	std::vector<ActiveFixedBinding> activeFixedBindings; // delta mode — polled in update()
	std::vector<LinkedFixedParam>   linkedFixedParams;   // absolute mode — listener-driven
	std::vector<PageGotoEntry>    pageGotoEntries;
	std::list<PageGotoListener>   pageGotoListeners; // std::list keeps pointers stable

	void loadMappings();
	void saveMappings();
	ofJson getMappingsJson() const;
	ofJson getAllProfilesJson();
	ofJson getProfileJson();
	void   saveProfileJson(const ofJson& updated);

	// ── MIDI Learn ────────────────────────────────────────────────────────────
	struct LearnedMessage {
		bool ready = false;
		int channel  = 0;
		int control  = 0;
		int status   = 0; // MIDI_CONTROL_CHANGE, MIDI_NOTE_ON, etc.
		int pitch    = 0;
		int value    = 0;
		std::string type; // "cc" | "note" | "pitch_bend"
	};

	struct MidiLearnListener : public ofxMidiListener {
		std::function<void(ofxMidiMessage&)> callback;
		void newMidiMessage(ofxMidiMessage& msg) override {
			if (callback) callback(msg);
		}
	};

	bool learnModeActive = false;
	LearnedMessage learnedMsg;
	std::vector<LearnedMessage> learnMessages; // rolling log while learn is active
	MidiLearnListener learnListener;
	std::mutex learnMutex;

	void startLearnMode();
	void stopLearnMode();
	void onLearnMessage(ofxMidiMessage& msg);
	void injectLearnMessage(int channel, int status, int control, int pitch, int value);

	// Re-applies bindings from disk without full MadMapper re-query
	void applyBindingsUpdate();
	std::atomic_bool hasPendingBindingsUpdate{false};

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
	void saveConfig(const ofJson& config);
	void applyPendingServerConfig();
	void refreshEndpointHealth(bool force = false);
	bool endpointReachable(const OscServerConfig& cfg, std::string* error = nullptr) const;

	std::vector<OscServerConfig> oscServerConfigs;
	std::vector<std::unique_ptr<ofxMadOscQuery>> extraOscQueries;
	std::vector<bool> endpointReachability;
	std::unordered_map<std::string, size_t> oscPathServerRouting;
	std::unique_ptr<WebServer> webServer;
	std::mutex pendingPageMutex;
	std::mutex pendingConfigMutex;
	std::mutex oscStateMutex;
	std::mutex activePageMutex;
	std::string pendingPageName;
	std::string activePageName;
	ofJson pendingConfig;
	bool hasPendingPageActivation = false;
	std::atomic_bool hasPendingReload{false};
	bool reloadRequested = false;
	uint64_t lastReloadMs = 0;
	bool hasPendingConfigUpdate = false;
	uint64_t lastEndpointHealthCheckMs = 0;
	std::atomic_bool healthCheckInProgress{false};
	std::atomic_bool hasPendingReconnect{false};
	std::atomic_bool reconnectInProgress{false};

	void tryConnectMidiDevice();
	void disconnectMidiDevice();
	uint64_t lastMidiScanMs = 0;
	std::vector<std::string> lastKnownInPorts;

	void refreshBonjourServices();
	std::vector<ofJson> bonjourDiscovered;
	std::mutex bonjourMutex;
	uint64_t lastBonjourScanMs = 0;
	std::atomic_bool bonjourScanInProgress{false};
};
