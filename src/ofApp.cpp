#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup(){
	// Setup midi controllers
	platformM.setup("Platform M+ V2.00");
	launchpad.setup("Launchpad");
	
	madOscQuery.setup("127.0.0.1", 8010, 8011);
	ofJson madmapperJson = madOscQuery.receive();
	
	madMapperLoadError = madmapperJson == nullptr;
	
	if(!madMapperLoadError){
		setupPages(madmapperJson);
	}
	errorImage.load("debug.png");
}

// CALBACK FUNCTIONS
// --------------------------------------------------------
void ofApp::selectSurface(string & name){
	if(currentPage->getName() == "opacity"){
		// Find the name of the corresponding surface
		auto result = ofSplitString(name, "_");
		int index = ofToInt(result[1]);
		std::list<MadParameter*>::iterator parameters = currentPage->getParameters()->begin();
		std::advance(parameters, index);
		string surface = (*parameters)->getName();
		
		std::list<MadParameterPage>::iterator pageIt;
		for(pageIt = pages.begin(); pageIt != pages.end(); pageIt++){
			if(pageIt->getName()==surface){
				MadParameterPage* prevPage = &(*currentPage);
				currentPage = pageIt;
				setActivePage(&(*currentPage), prevPage);
			}
		}
		
		oscSelectSurface(surface);
	}
}

void ofApp::selectMixer(float & p){
	MadParameterPage* prevPage = &(*currentPage);
	currentPage = pages.begin();
	setActivePage(&(*currentPage), prevPage);
	
	for(auto & midiComponent : selectGroup.midiComponents){
		midiComponent.second->value.disableEvents();
		midiComponent.second->value = 0;
		midiComponent.second->update();
		midiComponent.second->value.enableEvents();
	}
}

void ofApp::bankForward(float & p){
	if(next(currentPage) != pages.end() && p == 1){
		MadParameterPage* prevPage = &(*currentPage);
		currentPage++;
		setActivePage(&(*currentPage), prevPage);
	}
}

void ofApp::bankBackward(float & p){
	if(currentPage != pages.begin() && p == 1){
		MadParameterPage* prevPage = &(*currentPage);
		currentPage--;
		setActivePage(&(*currentPage), prevPage);
	}
}

void ofApp::removeListeners(){
	for(auto & page : pages) page.unlinkCycleControlComponents(platformM.midiComponents["chan_up"], platformM.midiComponents["chan_down"]);
	
	currentPage->unlinkDevice();
	
	platformM.midiComponents["bank_up"].value.removeListener(this, &ofApp::bankForward);
	platformM.midiComponents["bank_down"].value.removeListener(this, &ofApp::bankBackward);
	
	fadeToBlack->unlinkMidiComponent(platformM.midiComponents["fader_M"]);
	speed->unlinkMidiComponent(platformM.midiComponents["jog"]);
	
	ofRemoveListener(selectGroup.lastChangedE, this, &ofApp::selectSurface);
	ofRemoveListener(selectGroup.noneSelectedE, this, &ofApp::selectSurface);
	
}

// OSC FUNCTIONS
// -------------------------------------------------------------
void ofApp::oscSelectSurface(string name){
	string oscAddress = "/surfaces/"+name+"/select";
	ofxOscMessage m;
	m.setAddress(oscAddress);
	m.addFloatArg(1);
	madOscQuery.oscSendToMadMapper(m);
}

void ofApp::oscRequestMediaName(){
	string oscAddress = "/getControlValues?url=/medias/select_by_name";
	ofxOscMessage m;
	m.setAddress(oscAddress);
	m.addFloatArg(1);
	madOscQuery.oscSendToMadMapper(m);
}


//--------------------------------------------------------------
void ofApp::update(){
	if(!madMapperLoadError){
		ofSetWindowTitle((*currentPage).getName());
	}else{
		ofSetWindowTitle("MADMAPPER LOAD ERROR");
	}
}

void ofApp::setupPages(ofJson madmapperJson){
	// Create opacity pages
	madOscQuery.createOpacityPages(pages, &platformM, madmapperJson);
	
	// One for each Surface
	madOscQuery.createSurfacePages(pages, &platformM, madmapperJson);
	
	// One for each media
	madOscQuery.createMediaPages(mediaPages, &platformM, madmapperJson);
	
	// Set initial page
	currentPage = pages.begin();
	setActivePage(&(*currentPage), nullptr);
	
	// Setup all the specialised control.
	for(auto & page : pages) page.linkCycleControlComponents(platformM.midiComponents["chan_up"], platformM.midiComponents["chan_down"]);
	platformM.midiComponents["bank_up"].value.addListener(this, &ofApp::bankForward);
	platformM.midiComponents["bank_down"].value.addListener(this, &ofApp::bankBackward);
	platformM.midiComponents["mixer"].value.addListener(this, &ofApp::selectMixer);
	
	// Fixed Controlls
	fadeToBlack = madOscQuery.addParameter(madmapperJson["CONTENTS"]["master"]["CONTENTS"]["fade_to_black"]);
	fadeToBlack->linkMidiComponent(platformM.midiComponents["fader_M"]);
	
	speed = madOscQuery.addParameter(madmapperJson["CONTENTS"]["master"]["CONTENTS"]["GlobalBPM"]["CONTENTS"]["BPM"]);
	speed->linkMidiComponent(platformM.midiComponents["jog"]);
	
	// Select Group.
	selectGroup.doCheckbox = true;
	for(int i = 1; i<9; i++){
		selectGroup.add(platformM.midiComponents["sel_"+ofToString(i)]);
	}
	ofAddListener(selectGroup.lastChangedE, this, &ofApp::selectSurface);
	ofAddListener(selectGroup.noneSelectedE, this, &ofApp::selectMixer);
}

//--------------------------------------------------------------
void ofApp::draw(){
	if(!madMapperLoadError){
		//        platformM.drawRawInput();
		platformM.gui.setPosition(ofGetWidth()-230,10);
		platformM.gui.draw();
		
		launchpad.gui.setPosition(ofGetWidth()-460,10);
		launchpad.gui.draw();
		
		ofDrawBitmapStringHighlight(getStatusString(), 15, 15);
	}else{
		std::string s = "MADMAPPER HTTP ENDPOINT NOT FOUND - TRY AGAIN!";
		ofDrawBitmapStringHighlight(s, 15, 15);
		ofTranslate(ofGetWidth()/2, ofGetHeight()/2);
		errorImage.draw(-errorImage.getWidth()/2,-errorImage.getHeight()/2,errorImage.getWidth(), errorImage.getHeight());
	}
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
	float p = 1; // for use in callback functions
	
	if(key == 's'){
		//        platformM.saveMidiComponentsToFile("platformM.json");
	}
	
	if(key == ' '){
		auto success = reloadFromServer();
		madMapperLoadError = !success;
	}
	
	if(key == 'l'){
		platformM.setupFromFile("platformM.json");
	}
	
	if(key == 'o' && !madMapperLoadError){
		for(auto& p : *(*currentPage).getParameters()){
			std::cout << p->getParameterValue() << endl;
		}
	}
	
	if(key == OF_KEY_UP && !madMapperLoadError){
		bankForward(p);
	}
	if(key == OF_KEY_DOWN && !madMapperLoadError){
		bankBackward(p);
	}
	
	// Cycle through current page
	if(key == OF_KEY_LEFT && !madMapperLoadError){
		(*currentPage).cycleBackward(p);
	}
	if(key == OF_KEY_RIGHT && !madMapperLoadError){
		(*currentPage).cycleForward(p);
	}
}


//--------------------------------------------------------------
void ofApp::setActivePage(MadParameterPage* page, MadParameterPage* prevPage){
	if(prevPage != nullptr){
		prevPage->unlinkDevice();
	}
	page->linkDevice();
	
	ofLog() << "Active page set to " << (*currentPage).getName() << endl;
}

//--------------------------------------------------------------
std::string ofApp::getStatusString(){
	std::string s = "";
	s+= "Current page: " + (*currentPage).getName();
	s+= "\nRange: " + ofToString((*currentPage).getRange().first) + " " + ofToString((*currentPage).getRange().second);
	s+= "\nParameters on page:";
	
	int parNum = 1;
	for(auto& p : *(*currentPage).getParameters()){
		s+= "\n" + ofToString(parNum) + ") " + p->oscAddress + " " + ofToString(p->getParameterValue());
		parNum++;
	}
	return s;
}
//--------------------------------------------------------------
bool ofApp::reloadFromServer(){
	ofLog(OF_LOG_NOTICE) << "Attempting reload from server" << endl;
	// Save previous location
	std::string prevPageName = (*currentPage).getName();
	int prevLowerBound = (*currentPage).getRange().first;
	
	// Reloads parameters from MadMapper
	ofJson madmapperJson = madOscQuery.receive();
	if(madmapperJson == nullptr){
	ofLog(OF_LOG_WARNING) << "Reload unsuccessful!" << endl;
		return false;
	}
	// Setups pages
	removeListeners();
	selectGroup.clear();
	pages.clear();
	setupPages(madmapperJson);
	
	std::list<MadParameterPage>::iterator pageIt;
	for(pageIt = pages.begin(); pageIt != pages.end(); pageIt++){
		if(pageIt->getName() == prevPageName){
			MadParameterPage* prevPage = &(*currentPage);
			currentPage = pageIt;
			setActivePage(&(*currentPage), nullptr);
			(*currentPage).setLowerBound(prevLowerBound);
		}
	}
//
	// Assigns current page and range to previous
	// returns whether successful or not
	ofLog(OF_LOG_NOTICE) << "Reload successful!" << endl;
	return true;
}

//--------------------------------------------------------------
void ofApp::exit(){
	if(!madMapperLoadError)removeListeners();
}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){
	
}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y ){
	
}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button){
	
}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button){
	
}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){
	
}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y){
	
}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y){
	
}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h){
	
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){
	
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){
	
}




