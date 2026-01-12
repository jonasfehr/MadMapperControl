#include "ofApp.h"

void writeLogToWindow::log(ofLogLevel level, const string & module, const string & message){
	// print to cerr for OF_LOG_ERROR and OF_LOG_FATAL_ERROR, everything else to cout
	stringstream log;
	log << "[" << ofGetLogLevelName(level, true)  << "] ";
	// only print the module name if it's not ""
	if(module != ""){
		log << module << ": ";
	}
	log << message << std::endl;
	
	ofDrawBitmapString(log.str(), 0, 0);
}

static void noopDeleter_new(ofBaseLoggerChannel*){}

//--------------------------------------------------------------
void ofApp::setup() {
	ofSetFrameRate(60);
	// ofSetLoggerChannel(shared_ptr<writeLogToWindow>(new writeLogToWindow, std::function<void(ofBaseLoggerChannel *)>(noopDeleter_new)));
	// Setup midi controllers
	settings = ofLoadJson("settings.json");
	ip = "127.0.0.1";
	if (settings.contains("ip")) {
		ip = settings["ip"].get<std::string>();
	}
	
#ifdef TARGET_OS_OSX
	// platformM.setup("Platform M+ V2.12", "Platform M+ V2.12");
	// launchpad.setup("Launchpad", "Launchpad");
	/*
	 for (auto& m : faderport16.midiIn.getInPortList()) {
	 ofLog(OF_LOG_NOTICE) << "Found midi in device: " << m << endl;
	 }
	 for (auto& m : faderport16.midiOut.getOutPortList()) {
	 ofLog(OF_LOG_NOTICE) << "Found midi out device: " << m << endl;
	 }
	 */
	faderport16.setup("PreSonus FP16 Port 1", "PreSonus FP16 Port 1");
	faderport16XT.setup("PreSonus FP16 Port 2", "PreSonus FP16 Port 2");
#else
	// Windows
	for (auto& m : faderport16.midiIn.getPortList()) {
		ofLog(OF_LOG_NOTICE) << "Found midi in device: " << m << endl;
	}
	for (auto& m : faderport16.midiOut.getPortList()) {
		ofLog(OF_LOG_NOTICE) << "Found midi out device: " << m << endl;
	}
	faderport16.setup("PreSonus FP16 Port 1", "PreSonus FP16 Port 1");
	faderport16XT.setup("PreSonus FP16 Port 2", "PreSonus FP16 Port 2");
	
	
	// missing Launchpad Setup
#endif
	
	receivePort = PORT_RECEIVE;
		queryPort = PORT_RECEIVE;
	feedbackPort = PORT_FEEDBACK;
	
	if (settings.contains("receivePort")) {
		receivePort = settings["receivePort"].get<int>();
		queryPort = settings["receivePort"].get<int>();
	}
	
	if (settings.contains("queryPort")) {
			queryPort = settings["queryPort"].get<int>();
	}

	if (settings.contains("feedbackPort")) {
		feedbackPort = settings["feedbackPort"].get<int>();
	}
	
	
	madOscQuery.setup(ip, receivePort, feedbackPort, queryPort);
	

	gui.setup();
	// gui.add(swarmParameters);
	
	/*   MadParameterPage page = MadParameterPage("Flocking", &platformM);
	 //add PArameters
	 for(int i = 0; i<swarmParameters.getGroup("Flocking").size(); i++){
	 if(swarmParameters.getGroup("Flocking").getType(i) ==
	 "11ofParameterIfE"){ string name =
	 swarmParameters.getGroup("Flocking").getName(i); float value =
	 swarmParameters.getGroup("Flocking").getFloat(i);
	 //                    float min =
	 swarmParameters.getGroup("Flocking").get(i).getMin();
	 //                    float max =
	 swarmParameters.getGroup("Flocking").get(i).getmax();
	 //
	 ofJson param;
	 param["FULL_PATH"] = "/ParticleSystem/Flocking/"+name;
	 param["DESCRIPTION"] = name;
	 param["VALUE"][0] = value;
	 param["RANGE"][0] = {{"MIN", 0.},{"MAX", 1.}} ;
	 
	 std::string key = name;
	 madOscQuery.parameterMap[key] =
	 *madOscQuery.createParameter(param); auto mParam =
	 &madOscQuery.parameterMap.operator[](key); page.addParameter(mParam);
	 mParam->makeReferenceTo(swarmParameters.getGroup("Flocking").get(i).cast<float>());
	 }
	 }
	 if(!page.isEmpty()){
	 madOscQuery.pages.push_back(page);
	 }
	 
	 page = MadParameterPage("Visual", &platformM);
	 //add PArameters
	 for(int i = 0; i<swarmParameters.getGroup("Visual").size(); i++){
	 if(swarmParameters.getGroup("Visual").getType(i) ==
	 "11ofParameterIfE"){ string name =
	 swarmParameters.getGroup("Visual").getName(i); float value =
	 swarmParameters.getGroup("Visual").getFloat(i);
	 //                    float min =
	 swarmParameters.getGroup("Flocking").get(i).getMin();
	 //                    float max =
	 swarmParameters.getGroup("Flocking").get(i).getmax();
	 //
	 ofJson param;
	 param["FULL_PATH"] = "/ParticleSystem/Visual/"+name;
	 param["DESCRIPTION"] = name;
	 param["VALUE"][0] = value;
	 param["RANGE"][0] = {{"MIN", 0.},{"MAX", 1.}} ;
	 
	 std::string key = name;
	 madOscQuery.parameterMap[key] =
	 *madOscQuery.createParameter(param); auto mParam =
	 &madOscQuery.parameterMap.operator[](key); page.addParameter(mParam);
	 mParam->makeReferenceTo(swarmParameters.getGroup("Visual").get(i).cast<float>());
	 }
	 }
	 if(!page.isEmpty()){
	 madOscQuery.pages.push_back(page);
	 }
	 
	 */
	
	madOscQuery.receive();
	ofSleepMillis(100);
	
	if (madOscQuery.madMapperJson == nullptr) {
		ofLog(OF_LOG_WARNING) << "Load unsuccessful!" << endl;
	} else {
		//        madOscQuery.createParameterMap(madmapperJson);
		//        madOscQuery.createCustomPages(&platformM,
		//        ofLoadJson("custom_page.json"), madOscQuery.madMapperJson);
		
		float p = 1;
		//        setupPages(madmapperJson);
		//		setupUI(madmapperJson);
		auto success = reloadFromServer(p);
		madMapperLoadError = !success;
		initialised = true;
	}
	errorImage.load("debug.png");
}

// CALBACK FUNCTIONS
// --------------------------------------------------------
void ofApp::selectSurface(string& name) {
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
		while (i < (int)oscAddress.size() && oscAddress[i] != "opacity") { i++; }
		if (i == 0 || i >= (int)oscAddress.size()) return;
		string subpageName = oscAddress[i - 1];
		previousPage = currentPage;
		for (auto pageIt = madOscQuery.subPages.begin(); pageIt != madOscQuery.subPages.end(); ++pageIt) {
			if (pageIt->getName() == subpageName) {
				MadParameterPage* prevPage = &(*currentPage);
				currentPage = pageIt;
				setActivePage(&(*currentPage), prevPage);
				return;
			}
		}
		oscSelectSurface(subpageName);
	}
}

void ofApp::selectGroupContent(string& name) {
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
				MadParameterPage* prevPage = &(*currentPage);
				currentPage = pageIt;
				setActivePage(&(*currentPage), prevPage);
				return;
			}
		}
		oscSelectSurface(subpageName);
	}
}

void ofApp::selectMedia(string& name) {
	if ((*currentPage).isSubpage()) return;
	
	// Find the name of the corresponding surface
	auto result = ofSplitString(name, "_");
	int index = ofToInt(result[1]);
	if (currentPage->getParameters()->size() < index)
		return;  // catch if no parameter connected
	std::list<MadParameter*>::iterator parameter =
	currentPage->getParameters()->begin();
	std::advance(parameter, index - 1);
	if ((*parameter)->isSelectable()) {
		string subpageName = (*parameter)->getConnectedMediaName();
		
		previousPage = currentPage;
		
		std::list<MadParameterPage>::iterator pageIt;
		for (pageIt = madOscQuery.subPages.begin();
			 pageIt != madOscQuery.subPages.end(); pageIt++) {
			if (pageIt->getName() == subpageName) {
				MadParameterPage* prevPage = &(*currentPage);
				currentPage = pageIt;
				setActivePage(&(*currentPage), prevPage);
				return;
			}
		}
		
		oscSelectSurface(subpageName);
	}
}
void ofApp::showMedia(string& name) {
	ofRemoveListener(madOscQuery.mediaNameE, this, &ofApp::showMedia);
	
	if ((*currentPage).isSubpage()) return;
	previousPage = currentPage;
	
	std::list<MadParameterPage>::iterator pageIt;
	for (pageIt = madOscQuery.subPages.begin();
		 pageIt != madOscQuery.subPages.end(); pageIt++) {
		if (pageIt->getName() == name) {
			MadParameterPage* prevPage = &(*currentPage);
			currentPage = pageIt;
			setActivePage(&(*currentPage), prevPage);
			return;
		}
	}
	oscSelectSurface(name);
}

void ofApp::backToCurrent(float& p) {
	MadParameterPage* prevPage = &(*currentPage);
	currentPage = previousPage;
	setActivePage(&(*currentPage), prevPage);
	
	for (auto& midiComponent : selectGroup.midiComponents) {
		midiComponent.second->value.disableEvents();
		midiComponent.second->value = 0;
		midiComponent.second->update();
		midiComponent.second->value.enableEvents();
	}
}

// ======= CHANNEL CONTROL =======
void ofApp::chanForward(float& p) {
	if (p == 1) {
		(*currentPage).cycleForward();
	}
	updateParameterDisplay();
}

void ofApp::chanBackward(float& p) {
	if (p == 1) {
		(*currentPage).cycleBackward();
	}
	updateParameterDisplay();
}

// ======= BANK CONTROL =======
void ofApp::bankForward(float& p) {
	if (next(currentPage) != madOscQuery.pages.end() && p == 1) {
		MadParameterPage* prevPage = &(*currentPage);
		currentPage++;
		setActivePage(&(*currentPage), prevPage);
	}
}

void ofApp::bankBackward(float& p) {
	if (currentPage != madOscQuery.pages.begin() && p == 1) {
		MadParameterPage* prevPage = &(*currentPage);
		currentPage--;
		setActivePage(&(*currentPage), prevPage);
	}
}

void ofApp::reload(float& p) {
	if (p == 1) {
		isLoading = true;
		auto success = reloadFromServer(p);
		madMapperLoadError = !success;
	} else
		isLoading = false;
}

void ofApp::updateValues(float& p) {
	if (p == 1 && !isLoading) {
		isLoading = true;
		madOscQuery.updateValues();
	} else
		isLoading = false;
}

void ofApp::removeListeners() {
	currentPage->unlinkDevice();
	
	faderport16.midiComponents["chan_up"].value.removeListener(this,
															   &ofApp::chanForward);
	faderport16.midiComponents["chan_down"].value.removeListener(
																 this, &ofApp::chanBackward);
	faderport16.midiComponents["bank_up"].value.removeListener(this,
															   &ofApp::bankForward);
	faderport16.midiComponents["bank_down"].value.removeListener(
																 this, &ofApp::bankBackward);
	faderport16.midiComponents["rep"].value.removeListener(this,
														   &ofApp::updateValues);
	faderport16.midiComponents["mixer"].value.removeListener(this,
															 &ofApp::backToCurrent);
	
	fadeMasterVideo->unlinkMidiComponent(faderport16.midiComponents["fader_M_video"]);
	fadeMasterDMX->unlinkMidiComponent(faderport16.midiComponents["fader_M_dmx"]);
	fadeEngingeSpeed->unlinkMidiComponent(faderport16.midiComponents["fader_Speed"]);
	speed->unlinkMidiComponent(faderport16.midiComponents["jog"]);
	
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
	
	madOscQuery.oscReceiveMessages(swarmParameters);
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

static const ofJson* jsonGet(const ofJson &root, std::initializer_list<const char*> keys) {
	const ofJson* node = &root;
	for (auto k : keys) {
		if (!node->is_object()) return nullptr;
		auto it = node->find(k);
		if (it == node->end()) return nullptr;
		node = &(*it);
	}
	return node;
}

void ofApp::setupUI(ofJson madmapperJson) {
	// Setup all the specialised control.
	faderport16.midiComponents["chan_up"].value.addListener(this, &ofApp::chanForward);
	faderport16.midiComponents["chan_down"].value.addListener(this, &ofApp::chanBackward);
	faderport16.midiComponents["bank_up"].value.addListener(this, &ofApp::bankForward);
	faderport16.midiComponents["bank_down"].value.addListener(this, &ofApp::bankBackward);
	faderport16.midiComponents["mixer"].value.addListener(this, &ofApp::backToCurrent);
	faderport16.midiComponents["rep"].value.addListener(this, &ofApp::updateValues);

	// Masters (safe JSON access with fallbacks)
	const ofJson* mappingOpacity = jsonGet(madmapperJson, {"CONTENTS","surfaces","CONTENTS","Mapping","CONTENTS","opacity"});
	const ofJson* masterVideo = mappingOpacity ? mappingOpacity
											   : jsonGet(madmapperJson, {"CONTENTS","master","CONTENTS","master_video_level"});
	if (masterVideo) {
		fadeMasterVideo = madOscQuery.createParameter(*masterVideo);
		fadeMasterVideo->linkMidiComponent(faderport16.midiComponents["fader_M_video"]);
	}

	const ofJson* lightingOpacity = jsonGet(madmapperJson, {"CONTENTS","surfaces","CONTENTS","Lighting","CONTENTS","opacity"});
	const ofJson* masterDMX = lightingOpacity ? lightingOpacity
											  : jsonGet(madmapperJson, {"CONTENTS","master","CONTENTS","master_dmx_level"});
	if (masterDMX) {
		fadeMasterDMX = madOscQuery.createParameter(*masterDMX);
		fadeMasterDMX->linkMidiComponent(faderport16.midiComponents["fader_M_dmx"]);
	}

	const ofJson* engineSpeed = jsonGet(madmapperJson, {"CONTENTS","master","CONTENTS","engine_speed"});
	if (engineSpeed) {
		fadeEngingeSpeed = madOscQuery.createParameter(*engineSpeed);
		fadeEngingeSpeed->linkMidiComponent(faderport16.midiComponents["fader_Speed"]);
	}

	const ofJson* bpm = jsonGet(madmapperJson, {"CONTENTS","master","CONTENTS","Global_BPM","CONTENTS","BPM"});
	if (bpm) {
		speed = madOscQuery.createParameter(*bpm);
		speed->linkMidiComponent(faderport16.midiComponents["jog"]);
	}

	// Select Group.
	selectGroup.doCheckbox = true;
	for (int i = 1; i < 17; i++) {
		selectGroup.add(faderport16.midiComponents["sel_" + ofToString(i)]);
	}
	ofAddListener(selectGroup.lastChangedE, this, &ofApp::selectSurface);
	ofAddListener(selectGroup.noneSelectedE, this, &ofApp::backToCurrent);

	// Mute Group.
	muteGroup.doCheckbox = false;
	for (int i = 1; i < 17; i++) {
		muteGroup.add(faderport16.midiComponents["mute_" + ofToString(i)]);
	}
	ofAddListener(muteGroup.lastChangedE, this, &ofApp::selectSurface);
	ofAddListener(muteGroup.noneSelectedE, this, &ofApp::backToCurrent);

	// Solo Group.
	soloGroup.doCheckbox = true;
	for (int i = 1; i < 17; i++) {
		soloGroup.add(faderport16.midiComponents["solo_" + ofToString(i)]);
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
		} else {
			std::string s = "MADMAPPER HTTP ENDPOINT NOT FOUND - TRY AGAIN!";
			ofDrawBitmapStringHighlight(s, 15, 15);
			ofTranslate(ofGetWidth() / 2, ofGetHeight() / 2);
			errorImage.draw(-errorImage.getWidth() / 2, -errorImage.getHeight() / 2,
							errorImage.getWidth(), errorImage.getHeight());
		}
		if (showMidiIn) {
			faderport16.gui.setPosition(10, 10);
			faderport16.gui.draw();
		}
	}
	
	// gui.draw();
	
	stringstream windowInfo;
	windowInfo << "| MMCntrl | FPS: " << std::fixed
	<< std::setprecision(1) << ofGetFrameRate();
	windowInfo << " | " << ip << " (" <<     receivePort << "/" << feedbackPort <<"/" << queryPort <<") |";
	
	ofSetWindowTitle(windowInfo.str());
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
	float p = 1;  // for use in callback functions
	
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
		for (auto& p : *(*currentPage).getParameters()) {
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
		chanBackward(p);
	}
	if (key == OF_KEY_RIGHT && !madMapperLoadError) {
		chanForward(p);
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
void ofApp::setActivePage(MadParameterPage* page, MadParameterPage* prevPage) {
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
	s += "\nRange: " + ofToString((*currentPage).getRange().first) + " " +
	ofToString((*currentPage).getRange().second);
	s += "\nParameters on page:";
	
	int parNum = 1;
	for (auto& p : *(*currentPage).getParameters()) {
		s += "\n";
		if (parNum >= (*currentPage).getRange().first &&
			parNum <= (*currentPage).getRange().second)
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
bool ofApp::reloadFromServer(float& p) {
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
		madOscQuery.createCustomPages(&faderport16, ofLoadJson("custom_page.json"), madOscQuery.madMapperJson);
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
void ofApp::keyReleased(int key) {}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y) {}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button) {}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button) {}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button) {}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y) {}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y) {}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h) {}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg) {}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo) {}

void ofApp::updatePageDisplay() {
	// UPPER ROW_INPUT
	
	std::string str = (*currentPage).getName();
	
	int numOfLettersPerField = 5; // Mode 2 big Letters
	for(int i = 0; i*numOfLettersPerField < str.length(); i++){
		
		vector<unsigned char> text;
		text.push_back(0xF0);
		text.push_back(0x00);
		text.push_back(0x01);
		text.push_back(0x06);
		text.push_back(0x16); // DEVICE ID
		text.push_back(0x12);  // DISPLAY
		text.push_back(i);  // Channel
		text.push_back(0x00);  // line
		text.push_back(0x01);  // alignment (0:center, 1:left, 2:right)
		
		if((i+1)*numOfLettersPerField<str.length()){
			std::copy(str.begin()+i*numOfLettersPerField, str.begin()+(i+1)*numOfLettersPerField, std::back_inserter(text));
		}
		else{
			std::copy(str.begin()+i*numOfLettersPerField, str.end(), std::back_inserter(text));
		}
		
		text.push_back(0xF7);  // DETERMINATOR
		faderport16.midiOut.sendMidiBytes(text);
	}
	
	
	str = "SPD.";
	vector<unsigned char> text;
	text.push_back(0xF0);
	text.push_back(0x00);
	text.push_back(0x01);
	text.push_back(0x06);
	text.push_back(0x16); // DEVICE ID
	text.push_back(0x12);  // DISPLAY
	text.push_back(13);  // Channel
	text.push_back(0x00);  // line
	text.push_back(0x01);  // alignment (0:center, 1:left, 2:right)
	
	std::copy(str.begin(), str.end(), std::back_inserter(text));
	
	text.push_back(0xF7);  // DETERMINATOR
	faderport16.midiOut.sendMidiBytes(text);
	
	str = "DMX";
	text.clear();
	text.push_back(0xF0);
	text.push_back(0x00);
	text.push_back(0x01);
	text.push_back(0x06);
	text.push_back(0x16); // DEVICE ID
	text.push_back(0x12);  // DISPLAY
	text.push_back(14);  // Channel
	text.push_back(0x00);  // line
	text.push_back(0x01);  // alignment (0:center, 1:left, 2:right)
	
	std::copy(str.begin(), str.end(), std::back_inserter(text));
	
	text.push_back(0xF7);  // DETERMINATOR
	faderport16.midiOut.sendMidiBytes(text);
	
	str = "VID.";
	text.clear();
	text.push_back(0xF0);
	text.push_back(0x00);
	text.push_back(0x01);
	text.push_back(0x06);
	text.push_back(0x16); // DEVICE ID
	text.push_back(0x12);  // DISPLAY
	text.push_back(15);  // Channel
	text.push_back(0x00);  // line
	text.push_back(0x01);  // alignment (0:center, 1:left, 2:right)
	
	std::copy(str.begin(), str.end(), std::back_inserter(text));
	
	text.push_back(0xF7);  // DETERMINATOR
	faderport16.midiOut.sendMidiBytes(text);
	
	
}


void ofApp::setDisplayMode(int channel, bool clear = false) {
	// UPPER ROW_INPUT
	vector<unsigned char> text;
	text.push_back(0xF0);
	text.push_back(0x00);
	text.push_back(0x01);
	text.push_back(0x06);
	text.push_back(0x16);  // FaderPort16
	text.push_back(0x13);  // DISPLAY Mode Code
	text.push_back(channel);  // channel
	int mode = 1;
	text.push_back(mode+(clear<<4));  // mode
	/*
	 Default = 0
	 AlternativeDefault = 1
	 SmallText = 2
	 LargeText = 3
	 LargeTextMetering = 4
	 DefaultTextMetering = 5
	 MixedText = 6
	 AlternativeTextMetering = 7
	 MixedTextMetering = 8
	 Menu = 9
	 
	 ValueBarMode:
	 Normal = 0
	 Bipolar = 1
	 Fill = 2
	 Spread = 3
	 Off = 4
	 
	 */
	text.push_back(0xF7);  // DETERMINATOR
	faderport16.midiOut.sendMidiBytes(text);
}

void ofApp::updateParameterDisplay() {
	// Clear all displays first
	for(int i = 0; i < 16; i++){
		setDisplayMode(i, true);
	}
	updatePageDisplay();
	
	int parNum = 1;
	int i = 0;
	for (auto& p : *(*currentPage).getParameters()) {
		if (parNum >= (*currentPage).getRange().first &&
			parNum <= (*currentPage).getRange().second) {
			vector<unsigned char> text;
			text.push_back(0xF0);
			text.push_back(0x00);
			text.push_back(0x01);
			text.push_back(0x06);
			text.push_back(0x16); // DEVICE ID
			text.push_back(0x12);  // DISPLAY
			text.push_back(i);  // Channel
			text.push_back(0x01);  // line
			text.push_back(0x00);  // alignment
			// Build name from oscAddress using original flags
			string oscAddress = p->oscAddress;
			auto result = ofSplitString(oscAddress, "/");
			int maxNumChar = 9;
			string name;
			if (p->isMaster) {
				// Use parent segment
				if(result.size() >= 2) name = result[result.size() - 2];
			} else if (p->isModuleParameter) {
				// Use module segment (third from last)
				if(result.size() >= 3) name = result[result.size() - 3];
			} else {
				// Default to last segment
				if(!result.empty()) name = result.back();
			}
			int length = std::min<int>(name.size(), maxNumChar);
			std::copy(name.begin(), name.begin() + length, std::back_inserter(text));
			for (int fill = length; fill < maxNumChar; ++fill) text.push_back(' ');
			text.push_back(0xF7);  // DETERMINATOR
			faderport16.midiOut.sendMidiBytes(text);
			i++;
		}
		parNum++;
	}
	
}
