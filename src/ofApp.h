#pragma once

#include "DeviceProfile.h"
#include "Faderport16Surface.h"
#include "MidiControlSurface.h"
#include "OscServerManager.h"
#include "PlatformMSurface.h"
#include "Push3Surface.h"
#include "WebServer.h"
#include "ofMain.h"
#include "ofxMadOscQuery.h"
#include "ofxMidiDevice.h"
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <atomic>
#include <thread>
#include <unordered_map>

class ofApp : public ofBaseApp {
  public:
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
	OscServerManager oscServers;
	ofxMadOscQuery& madOscQuery = oscServers.primary(); // primary MadMapper connection
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
	void selectMedia(string& name);
	void triggerCue(const CueGridItem& cue);
	void onWebSocketPathUpdate(std::string& path);
	void subscribeTimelinePaths();
	void backToCurrent(float& p);
	bool reloadFromServer(float& p);
	void updateValues(float& p);

	bool isLoading;
	bool showMidiIn = false;
	bool showStatusString = false;
	bool noDeviceConnected = false; // true when no MIDI profile matches connected ports
	bool virtualSurface = false;    // surface created from profile without MIDI ports (web emulator)

	// OSC functions
	void oscSelectSurface(string name);
	void oscSelectSurface(string name, size_t serverId);
	void oscSelectMedia(string name);
	void oscSelectMedia(string name, size_t serverId);

	void removeListeners();

	TimelineGridState timelineGridState;
	std::vector<std::string> availableCueBanks;
	std::string cueBankName = "Bank-1";
	bool cueFollowActiveBank = true;
	bool cueGridActive = false;
	std::atomic<bool> cueGridRefreshPending{false};
	uint64_t lastCueGridRefreshMs = 0;

	// TD hover encoder — Push3 master encoder routes relative MIDI to the
	// currently hovered TouchDesigner parameter via OSC. Wired as a regular
	// delta-mode fixed binding (see loadMappings), only with faster acceleration.
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
		float          accelBase = 6.f;
		float          accelMax  = 8.f;
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
	// Global scale on outgoing deltas (matches MadParameter::encoderSensitivity).
	static constexpr float kEncoderSensitivity = 0.8f;

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

	// ── Emulator bridge ───────────────────────────────────────────────────────
	// The web emulator substitutes the hardware controller: it injects MIDI into
	// the same processing chain and mirrors the device display state.
	struct DisplaySnapshot {
		std::string page;
		std::string profileName;
		bool isVirtual = false;
		std::vector<std::string> labels;
		std::vector<float> values;
	};
	DisplaySnapshot displaySnapshot;
	TimelineGridState displayCueGrid;
	std::mutex displayMutex;
	ofJson getDisplayJson();
	void injectMidiMessage(const ofJson& body);
	// Injected MIDI is queued on the HTTP thread and drained on the main thread,
	// so it never races with page changes or a surface swap (which fire the same
	// component listeners and rebuild the surface).
	std::mutex midiInjectMutex;
	std::vector<ofxMidiMessage> injectedMidiQueue;
	void drainInjectedMidi();

	// Which profile the virtual surface emulates when no hardware is connected.
	std::string emulatorProfileName;
	// Web can request a different virtual surface; the swap runs on the main thread.
	void requestEmulatorSurface(const ofJson& body);
	std::mutex emulatorMutex;
	std::string pendingEmulatorProfile;
	std::atomic_bool hasPendingEmulatorSwitch{false};

  private:
	// Shared core of selectSurface/selectMedia: opens the matching subpage or
	// falls back to a raw OSC select message.
	void selectSubpageFromButton(const std::string& buttonName,
	                             const std::string& skipRoleSuffix,
	                             const std::string& oscPrefix,
	                             const std::function<std::string(MadParameter*)>& subpageNameFor,
	                             const std::function<void(const std::string&)>& oscFallback);

	// Web Server API methods
	void setupWebServer();
	ofJson getPages();
	void savePages(const ofJson& pages);
	ofJson getParameters();
	ofJson getConfig();
	void saveConfig(const ofJson& config);
	void applyPendingServerConfig();

	std::unique_ptr<WebServer> webServer;
	std::mutex pendingPageMutex;
	std::mutex pendingConfigMutex;
	std::mutex activePageMutex;
	std::string pendingPageName;
	std::string activePageName;
	ofJson pendingConfig;
	bool hasPendingPageActivation = false;
	std::atomic_bool hasPendingReload{false};
	bool reloadRequested = false;
	uint64_t lastReloadMs = 0;
	bool hasPendingConfigUpdate = false;
	std::atomic_bool hasPendingReconnect{false};
	std::atomic_bool reconnectInProgress{false};

	void tryConnectMidiDevice();
	void disconnectMidiDevice();
	uint64_t lastMidiScanMs = 0;
	std::vector<std::string> lastKnownInPorts;
};
