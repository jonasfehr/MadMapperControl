#include "ofApp.h"
#include <algorithm>

static MidiComponent * getComponentByRole(ofxMidiDevice * dev, const std::string & role) {
	if (!dev) return nullptr;
	auto it = dev->bindings.find(role);
	if (it == dev->bindings.end()) return nullptr;
	auto mc = dev->midiComponents.find(it->second);
	if (mc == dev->midiComponents.end()) return nullptr;
	return &mc->second;
}

//--------------------------------------------------------------
void ofApp::setup() {
	ofSetFrameRate(60);
	settings = ofLoadJson("settings.json");
	ip = settings.contains("ip") ? settings["ip"].get<std::string>() : "127.0.0.1";

	// Load profiles and select first matching connected device
	auto profilesOpt = loadDeviceProfiles("device_profiles.json");
	if (profilesOpt) {
		const auto inPorts = ofxMidiIn().getInPortList();
		const auto outPorts = ofxMidiOut().getOutPortList();
		for (const auto & p : *profilesOpt) {
			bool inMatch = std::find(inPorts.begin(), inPorts.end(), p.midiInPort) != inPorts.end();
			bool outMatch = std::find(outPorts.begin(), outPorts.end(), p.midiOutPort) != outPorts.end();
			if (inMatch && outMatch) {
				activeProfile = p;
				break;
			}
		}
	}

	if (activeProfile) {
		noDeviceConnected = false;
		if (activeProfile->name.find("Push") != std::string::npos)
			surface = std::make_unique<Push3Surface>();
		else if (activeProfile->name.find("Platform") != std::string::npos)
			surface = std::make_unique<PlatformMSurface>();
		else
			surface = std::make_unique<Faderport16Surface>();
		static_cast<ofxMidiDevice *>(surface.get())->setupFromProfile(*activeProfile);
	} else {
		noDeviceConnected = true;
		ofLogWarning() << "No MIDI device/profile matched. Running without controller.";
		surface.reset();
	}

	sendPort = PORT_RECEIVE;
	queryPort = PORT_RECEIVE;
	feedbackPort = PORT_FEEDBACK;
	if (settings.contains("sendPort")) {
		sendPort = settings["sendPort"].get<int>();
		queryPort = settings["sendPort"].get<int>();
	}
	if (settings.contains("queryPort")) queryPort = settings["queryPort"].get<int>();
	if (settings.contains("feedbackPort")) feedbackPort = settings["feedbackPort"].get<int>();

	madOscQuery.setup(ip, sendPort, feedbackPort, queryPort);
	gui.setup();
	madOscQuery.receive();
	ofSleepMillis(100);
	if (madOscQuery.madMapperJson == nullptr) {
		ofLog(OF_LOG_WARNING) << "Load unsuccessful!" << endl;
	} else {
		float p = 1;
		auto success = reloadFromServer(p);
		madMapperLoadError = !success;
		initialised = true;
	}
	errorImage.load("debug.png");
}

// CALBACK FUNCTIONS
// --------------------------------------------------------
void ofApp::selectSurface(string & name) {
	if ((*currentPage).isSubpage()) return;
	auto result = ofSplitString(name, "_");
	if (result.size() < 2) return;
	int index = ofToInt(result[1]);
	if (currentPage->getParameters()->size() < index) return;
	auto parameter = currentPage->getParameters()->begin();
	std::advance(parameter, index - 1);
	if ((*parameter)->isSelectable()) {
		auto oscAddress = ofSplitString((*parameter)->getOscAddress(), "/");
		int i = 0;
		while (i < (int)oscAddress.size() && oscAddress[i] != "opacity") {
			i++;
		}
		if (i == 0 || i >= (int)oscAddress.size()) return;
		string subpageName = oscAddress[i - 1];
		previousPage = currentPage;
		for (auto pageIt = madOscQuery.subPages.begin(); pageIt != madOscQuery.subPages.end(); ++pageIt) {
			if (pageIt->getName() == subpageName) {
				MadParameterPage * prevPage = &(*currentPage);
				currentPage = pageIt;
				setActivePage(&(*currentPage), prevPage);
				return;
			}
		}
		oscSelectSurface(subpageName);
	}
}

void ofApp::selectGroupContent(string & name) {
	auto result = ofSplitString(name, "_");
	if (result.size() < 2) return;
	int index = ofToInt(result[1]);
	if (currentPage->getParameters()->size() < index) return;
	auto parameter = currentPage->getParameters()->begin();
	std::advance(parameter, index - 1);
	if ((*parameter)->isSelectable() && (*parameter)->isGroup()) {
		auto oscAddress = ofSplitString((*parameter)->getOscAddress(), "/");
		if (oscAddress.size() < 3) return;
		string subpageName = oscAddress[2];
		if ((*currentPage).isSubpage()) return;
		previousPage = currentPage;
		for (auto pageIt = madOscQuery.subPages.begin(); pageIt != madOscQuery.subPages.end(); ++pageIt) {
			if (pageIt->getName() == subpageName + "_SubPage") {
				MadParameterPage * prevPage = &(*currentPage);
				currentPage = pageIt;
				setActivePage(&(*currentPage), prevPage);
				return;
			}
		}
		oscSelectSurface(subpageName);
	}
}

void ofApp::selectMedia(string & name) {
	if ((*currentPage).isSubpage()) return;

	// Find the name of the corresponding surface
	auto result = ofSplitString(name, "_");
	int index = ofToInt(result[1]);
	if (currentPage->getParameters()->size() < index)
		return; // catch if no parameter connected
	std::list<MadParameter *>::iterator parameter = currentPage->getParameters()->begin();
	std::advance(parameter, index - 1);
	if ((*parameter)->isSelectable()) {
		string subpageName = (*parameter)->getConnectedMediaName();

		previousPage = currentPage;

		std::list<MadParameterPage>::iterator pageIt;
		for (pageIt = madOscQuery.subPages.begin();
			pageIt != madOscQuery.subPages.end(); pageIt++) {
			if (pageIt->getName() == subpageName) {
				MadParameterPage * prevPage = &(*currentPage);
				currentPage = pageIt;
				setActivePage(&(*currentPage), prevPage);
				return;
			}
		}

		oscSelectSurface(subpageName);
	}
}
void ofApp::showMedia(string & name) {
	ofRemoveListener(madOscQuery.mediaNameE, this, &ofApp::showMedia);

	if ((*currentPage).isSubpage()) return;
	previousPage = currentPage;

	std::list<MadParameterPage>::iterator pageIt;
	for (pageIt = madOscQuery.subPages.begin();
		pageIt != madOscQuery.subPages.end(); pageIt++) {
		if (pageIt->getName() == name) {
			MadParameterPage * prevPage = &(*currentPage);
			currentPage = pageIt;
			setActivePage(&(*currentPage), prevPage);
			return;
		}
	}
	oscSelectSurface(name);
}

void ofApp::backToCurrent(float & p) {
	MadParameterPage * prevPage = &(*currentPage);
	currentPage = previousPage;
	setActivePage(&(*currentPage), prevPage);

	for (auto & midiComponent : selectGroup.midiComponents) {
		midiComponent.second->value.disableEvents();
		midiComponent.second->value = 0;
		midiComponent.second->update();
		midiComponent.second->value.enableEvents();
	}
}

// ======= CHANNEL CONTROL =======
void ofApp::pageForward(float & p) {
	if (p == 1) {
		(*currentPage).cycleForward();
	}
	updateParameterDisplay();
}

void ofApp::pageBackward(float & p) {
	if (p == 1) {
		(*currentPage).cycleBackward();
	}
	updateParameterDisplay();
}

// ======= BANK CONTROL =======
void ofApp::bankForward(float & p) {
	if (next(currentPage) != madOscQuery.pages.end() && p == 1) {
		MadParameterPage * prevPage = &(*currentPage);
		currentPage++;
		setActivePage(&(*currentPage), prevPage);
	}
}

void ofApp::bankBackward(float & p) {
	if (currentPage != madOscQuery.pages.begin() && p == 1) {
		MadParameterPage * prevPage = &(*currentPage);
		currentPage--;
		setActivePage(&(*currentPage), prevPage);
	}
}

void ofApp::reload(float & p) {
	if (p == 1) {
		isLoading = true;
		auto success = reloadFromServer(p);
		madMapperLoadError = !success;
	} else
		isLoading = false;
}

void ofApp::updateValues(float & p) {
	if (p == 1 && !isLoading) {
		isLoading = true;
		madOscQuery.updateValues();
		updateParameterDisplay(); // refresh display with updated values
	} else
		isLoading = false;
}

void ofApp::removeListeners() {
	if (!surface) return;
	auto * dev = static_cast<ofxMidiDevice *>(surface.get());
	currentPage->unlinkDevice();
	if (auto c = ::getComponentByRole(dev, "nav.pageNext")) c->value.removeListener(this, &ofApp::pageForward);
	if (auto c = ::getComponentByRole(dev, "nav.pagePrev")) c->value.removeListener(this, &ofApp::pageBackward);
	if (auto c = ::getComponentByRole(dev, "nav.bankNext")) c->value.removeListener(this, &ofApp::bankForward);
	if (auto c = ::getComponentByRole(dev, "nav.bankPrev")) c->value.removeListener(this, &ofApp::bankBackward);
	if (auto c = ::getComponentByRole(dev, "nav.reload")) c->value.removeListener(this, &ofApp::updateValues);
	if (auto c = ::getComponentByRole(dev, "action.back")) c->value.removeListener(this, &ofApp::backToCurrent);
	if (dev->midiComponents.count("arrow_page_left")) dev->midiComponents["arrow_page_left"].value.removeListener(this, &ofApp::pageBackward);
	if (dev->midiComponents.count("arrow_page_right")) dev->midiComponents["arrow_page_right"].value.removeListener(this, &ofApp::pageForward);

	if (dev->midiComponents.count("fader_M_video")) fadeMasterVideo->unlinkMidiComponent(dev->midiComponents["fader_M_video"]);
	if (dev->midiComponents.count("fader_M_dmx")) fadeMasterDMX->unlinkMidiComponent(dev->midiComponents["fader_M_dmx"]);
	if (dev->midiComponents.count("fader_Speed")) fadeEngingeSpeed->unlinkMidiComponent(dev->midiComponents["fader_Speed"]);
	if (dev->midiComponents.count("jog")) speed->unlinkMidiComponent(dev->midiComponents["jog"]);

	ofRemoveListener(selectGroup.lastChangedE, this, &ofApp::selectSurface);
	ofRemoveListener(selectGroup.noneSelectedE, this, &ofApp::backToCurrent);
	ofRemoveListener(muteGroup.lastChangedE, this, &ofApp::selectSurface);
	ofRemoveListener(muteGroup.noneSelectedE, this, &ofApp::backToCurrent);
	ofRemoveListener(soloGroup.lastChangedE, this, &ofApp::selectMedia);
	ofRemoveListener(soloGroup.noneSelectedE, this, &ofApp::backToCurrent);
}

// OSC FUNCTIONS
// -------------------------------------------------------------
void ofApp::oscSelectSurface(string name) {
	string oscAddress = "/surfaces/" + name + "/select";

	ofxOscMessage m;
	m.setAddress(oscAddress);
	m.addFloatArg(1);
	madOscQuery.oscSendToMadMapper(m);
}

void ofApp::oscSelectMedia(string name) {
	string oscAddress = "/medias/select_by_name";

	ofxOscMessage m;
	m.setAddress(oscAddress);
	m.addStringArg(name);
	madOscQuery.oscSendToMadMapper(m);
}

void ofApp::oscRequestMediaName() {
	string oscAddress = "/getControlValues?url=/medias/select_by_name";
	ofxOscMessage m;
	m.setAddress(oscAddress);
	m.addFloatArg(1);
	madOscQuery.oscSendToMadMapper(m);
}

//--------------------------------------------------------------
void ofApp::update() {
	if (!isLoading) {
		if (!madMapperLoadError) {
			ofSetWindowTitle("MADMAPPER MIDI MAPPER");
		} else {
			ofSetWindowTitle("MADMAPPER LOAD ERROR");
		}
	}

	oscParamSync.update();
}

void ofApp::setupPages(ofJson madmapperJson) {
	//    // Custom
	//    madOscQuery.createCustomPage(pages, &platformM,
	//    ofLoadJson("custom_page.json"));
	//
	//    // One for each possible Subpage
	//    madOscQuery.createSubPages(subPages, &platformM, madmapperJson);

	//    cout << "subPages " << subPages.size() << endl;

	// Set initial page
	currentPage = madOscQuery.pages.begin();
	previousPage = currentPage;

	setActivePage(&(*currentPage), nullptr);
}

static const ofJson * jsonGet(const ofJson & root, std::initializer_list<const char *> keys) {
	const ofJson * node = &root;
	for (auto k : keys) {
		if (!node->is_object()) return nullptr;
		auto it = node->find(k);
		if (it == node->end()) return nullptr;
		node = &(*it);
	}
	return node;
}

void ofApp::setupUI(ofJson madmapperJson) {
	if (!surface) return;
	auto * dev = static_cast<ofxMidiDevice *>(surface.get());
	if (auto c = ::getComponentByRole(dev, "nav.pageNext")) c->value.addListener(this, &ofApp::pageForward);
	if (auto c = ::getComponentByRole(dev, "nav.pagePrev")) c->value.addListener(this, &ofApp::pageBackward);
	if (auto c = ::getComponentByRole(dev, "nav.bankNext")) c->value.addListener(this, &ofApp::bankForward);
	if (auto c = ::getComponentByRole(dev, "nav.bankPrev")) c->value.addListener(this, &ofApp::bankBackward);
	if (auto c = ::getComponentByRole(dev, "nav.reload")) c->value.addListener(this, &ofApp::updateValues);
	if (auto c = ::getComponentByRole(dev, "action.back")) c->value.addListener(this, &ofApp::backToCurrent);
	if (dev->midiComponents.count("arrow_page_left")) dev->midiComponents["arrow_page_left"].value.addListener(this, &ofApp::pageBackward);
	if (dev->midiComponents.count("arrow_page_right")) dev->midiComponents["arrow_page_right"].value.addListener(this, &ofApp::pageForward);

	const ofJson * mappingOpacity = jsonGet(madmapperJson, { "CONTENTS", "surfaces", "CONTENTS", "Mapping", "CONTENTS", "opacity" });
	const ofJson * masterVideo = mappingOpacity ? mappingOpacity : jsonGet(madmapperJson, { "CONTENTS", "master", "CONTENTS", "master_video_level" });
	if (masterVideo && dev->midiComponents.count("fader_M_video")) {
		fadeMasterVideo = madOscQuery.createParameter(*masterVideo);
		fadeMasterVideo->linkMidiComponent(dev->midiComponents["fader_M_video"]);
	}
	const ofJson * lightingOpacity = jsonGet(madmapperJson, { "CONTENTS", "surfaces", "CONTENTS", "Lighting", "CONTENTS", "opacity" });
	const ofJson * masterDMX = lightingOpacity ? lightingOpacity : jsonGet(madmapperJson, { "CONTENTS", "master", "CONTENTS", "master_dmx_level" });
	if (masterDMX && dev->midiComponents.count("fader_M_dmx")) {
		fadeMasterDMX = madOscQuery.createParameter(*masterDMX);
		fadeMasterDMX->linkMidiComponent(dev->midiComponents["fader_M_dmx"]);
	}
	const ofJson * engineSpeed = jsonGet(madmapperJson, { "CONTENTS", "master", "CONTENTS", "engine_speed" });
	if (engineSpeed && dev->midiComponents.count("fader_Speed")) {
		fadeEngingeSpeed = madOscQuery.createParameter(*engineSpeed);
		fadeEngingeSpeed->linkMidiComponent(dev->midiComponents["fader_Speed"]);
	}
	const ofJson * bpm = jsonGet(madmapperJson, { "CONTENTS", "master", "CONTENTS", "Global_BPM", "CONTENTS", "BPM" });
	if (bpm && dev->midiComponents.count("jog")) {
		speed = madOscQuery.createParameter(*bpm);
		speed->linkMidiComponent(dev->midiComponents["jog"]);
	}

	selectGroup.doCheckbox = true;
	for (int i = 1; i < 17; i++) {
		std::string lbl = "sel_" + ofToString(i);
		if (dev->midiComponents.count(lbl)) selectGroup.add(dev->midiComponents[lbl]);
	}
	ofAddListener(selectGroup.lastChangedE, this, &ofApp::selectSurface);
	ofAddListener(selectGroup.noneSelectedE, this, &ofApp::backToCurrent);

	muteGroup.doCheckbox = false;
	for (int i = 1; i < 17; i++) {
		std::string lbl = "mute_" + ofToString(i);
		if (dev->midiComponents.count(lbl)) muteGroup.add(dev->midiComponents[lbl]);
	}
	ofAddListener(muteGroup.lastChangedE, this, &ofApp::selectSurface);
	ofAddListener(muteGroup.noneSelectedE, this, &ofApp::backToCurrent);

	soloGroup.doCheckbox = true;
	for (int i = 1; i < 17; i++) {
		std::string lbl = "solo_" + ofToString(i);
		if (dev->midiComponents.count(lbl)) soloGroup.add(dev->midiComponents[lbl]);
	}
	ofAddListener(soloGroup.lastChangedE, this, &ofApp::selectMedia);
	ofAddListener(soloGroup.noneSelectedE, this, &ofApp::backToCurrent);
}

//--------------------------------------------------------------
void ofApp::draw() {
	if (!isLoading) {
		if (!madMapperLoadError) {
			ofBackground(0);
			if (showStatusString) drawStatusString();
			if (noDeviceConnected) {
				ofDrawBitmapStringHighlight("No MIDI controller connected", 15, 30);
			}
		} else {
			std::string s = "MADMAPPER HTTP ENDPOINT NOT FOUND - TRY AGAIN!";
			ofDrawBitmapStringHighlight(s, 15, 15);
			ofTranslate(ofGetWidth() / 2, ofGetHeight() / 2);
			errorImage.draw(-errorImage.getWidth() / 2, -errorImage.getHeight() / 2,
				errorImage.getWidth(), errorImage.getHeight());
		}
		if (showMidiIn && surface) {
			auto * dev = static_cast<ofxMidiDevice *>(surface.get());
			dev->gui.setPosition(10, 10);
			dev->gui.draw();
		}
	}

	// gui.draw();

	stringstream windowInfo;
	windowInfo << "| MMCntrl | FPS: " << std::fixed
			   << std::setprecision(1) << ofGetFrameRate();
	windowInfo << " | " << ip << " (" << sendPort << "/" << feedbackPort << "/" << queryPort << ") |";
	if (noDeviceConnected) windowInfo << " NO MIDI";

	ofSetWindowTitle(windowInfo.str());

	if (ofGetFrameNum() % 10 == 0 && !noDeviceConnected) {
		updateParameterDisplay();
	}
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
	float p = 1; // for use in callback functions

	if (key == 's') {
		showStatusString = !showStatusString;

		//        platformM.saveMidiComponentsToFile("platformM.json");
	}
	if (key == 'm') {
		showMidiIn = !showMidiIn;
	}

	if (key == ' ') {
		//        auto success = reloadFromServer(p);
		//        madMapperLoadError = !success;
		madOscQuery.updateValues();
	}

	if (key == 'l') {
		//platformM.setupFromFile("platformM.json");
	}

	if (key == 'o' && !madMapperLoadError) {
		for (auto & p : *(*currentPage).getParameters()) {
			std::cout << p->getParameterValue() << endl;
		}
	}

	if (key == OF_KEY_UP && !madMapperLoadError) {
		bankForward(p);
	}
	if (key == OF_KEY_DOWN && !madMapperLoadError) {
		bankBackward(p);
	}

	// Cycle through current page
	if (key == OF_KEY_LEFT && !madMapperLoadError) {
		pageBackward(p);
	}
	if (key == OF_KEY_RIGHT && !madMapperLoadError) {
		pageForward(p);
	}

	if (key > 48 && key < 58) {
		int index = key - 48;
		string name = "solo_" + ofToString(index);
		selectMedia(name);
	}
	if (key == '0') {
		float p = 1;
		backToCurrent(p);
	}
}

//--------------------------------------------------------------
void ofApp::setActivePage(MadParameterPage * page, MadParameterPage * prevPage) {
	if (prevPage != nullptr) {
		prevPage->unlinkDevice();
	}
	page->linkDevice();

	ofLog() << "Active page set to " << (*currentPage).getName() << endl;

	updatePageDisplay();
	updateParameterDisplay();
}

//--------------------------------------------------------------
void ofApp::drawStatusString() {
	std::string s = "";
	s += "Current page: " + (*currentPage).getName();
	s += "\nRange: " + ofToString((*currentPage).getRange().first) + " " + ofToString((*currentPage).getRange().second);
	s += "\nParameters on page:";

	int parNum = 1;
	for (auto & p : *(*currentPage).getParameters()) {
		s += "\n";
		if (parNum >= (*currentPage).getRange().first && parNum <= (*currentPage).getRange().second)
			s += "* ";
		else
			s += "  ";
		s += ofToString(parNum) + ") ";
		if (p->isGroup()) s += "G ";
		s += p->oscAddress + " " + ofToString(p->getParameterValue());
		parNum++;
	}
	ofDrawBitmapString(s, 15, 15);
}
//--------------------------------------------------------------
bool ofApp::reloadFromServer(float & p) {
	if (p == 1) {
		ofLog(OF_LOG_NOTICE) << "Attempting reload from server" << endl;
		// Save previous location
		std::string prevPageName = "";
		int prevLowerBound = 1;
		if (initialised && currentPage != madOscQuery.pages.end()) {
			prevPageName = (*currentPage).getName();
			prevLowerBound = (*currentPage).getRange().first;
		}
		madOscQuery.receive();

		if (madOscQuery.madMapperJson == nullptr) {
			ofLog(OF_LOG_WARNING) << "Reload unsuccessful!" << endl;
			return false;
		}

		// If we were initialised already, clear old bindings and pages first
		if (initialised) {
			removeListeners();
			selectGroup.clear();
			muteGroup.clear();
			soloGroup.clear();
			madOscQuery.pages.clear();
			madOscQuery.subPages.clear();
		}

		// Rebuild pages and UI
		madOscQuery.createCustomPages(static_cast<ofxMidiDevice *>(surface.get()), ofLoadJson("custom_page.json"), madOscQuery.madMapperJson);
		setupUI(madOscQuery.madMapperJson);

		// Try to restore previous page
		for (auto pageIt = madOscQuery.pages.begin(); pageIt != madOscQuery.pages.end(); ++pageIt) {
			if (pageIt->getName() == prevPageName) {
				currentPage = pageIt;
				setActivePage(&(*currentPage), nullptr);
				(*currentPage).setLowerBound(prevLowerBound);
				ofLog(OF_LOG_NOTICE) << "Reload successful and resuming from " << prevPageName << endl;
				return true;
			}
		}
		ofLog(OF_LOG_NOTICE) << "Reload successful assigning to first page!" << endl;
		if (!madOscQuery.pages.empty()) {
			currentPage = madOscQuery.pages.begin();
			setActivePage(&(*currentPage), nullptr);
		}
		return true;
	} else {
		return false;
	}
}

//--------------------------------------------------------------
void ofApp::exit() {
	if (!madMapperLoadError) removeListeners();
}

//--------------------------------------------------------------
void ofApp::keyReleased(int key) { }

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y) { }

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button) { }

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button) { }

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button) { }

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y) { }

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y) { }

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h) { }

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg) { }

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo) { }

void ofApp::updatePageDisplay() {
	if (surface) surface->updatePageDisplay((*currentPage).getName());
}

void ofApp::updateParameterDisplay() {
	std::vector<std::string> labels;
	std::vector<float> values;
	int parNum = 1;
	for (auto & p : *(*currentPage).getParameters()) {
		if (parNum >= (*currentPage).getRange().first && parNum <= (*currentPage).getRange().second) {
			string oscAddress = p->oscAddress;
			auto result = ofSplitString(oscAddress, "/");
			string name;
			if (p->isMaster && result.size() >= 2)
				name = result[result.size() - 2];
			else if (p->isModuleParameter && result.size() >= 3)
				name = result[result.size() - 3];
			else if (!result.empty())
				name = result.back();
			labels.push_back(name);
			values.push_back(p->get());
		}
		parNum++;
	}
	if (surface) surface->updateParameterDisplay(labels, values);
}
