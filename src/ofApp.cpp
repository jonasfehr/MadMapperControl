#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup(){
	// Setup midi controllers
#ifdef TARGET_OS_OSX
	platformM.setup("Platform M+ V2.00", "Platform M+ V2.00");
#else
	// Windows
	for(auto& m : platformM.midiIn.getPortList()){
		ofLog(OF_LOG_NOTICE) << "Found midi in device: " << m << endl;
	}
	for (auto& m : platformM.midiOut.getPortList()) {
		ofLog(OF_LOG_NOTICE) << "Found midi out device: " << m << endl;
	}
	platformM.setup("Platform M+ V2.00 0", "Platform M+ V2.00 1");
#endif


	madOscQuery.setup("127.0.0.1", 8010, 8011);
    ofSleepMillis(100);


    if(madOscQuery.madMapperJson == nullptr){
        ofLog(OF_LOG_WARNING) << "Load unsuccessful!" << endl;
    }else{
        
//        madOscQuery.createParameterMap(madmapperJson);
//        madOscQuery.createCustomPages(&platformM, ofLoadJson("custom_page.json"), madOscQuery.madMapperJson);

        
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
void ofApp::selectSurface(string & name){
    
    if((*currentPage).isSubpage()) return;

    // Find the name of the corresponding surface
    auto result = ofSplitString(name, "_");
    int index = ofToInt(result[1]);
    if(currentPage->getParameters()->size()<index) return; // catch if no parameter connected
    std::list<MadParameter*>::iterator parameter = currentPage->getParameters()->begin();
    std::advance(parameter, index-1);
        if((*parameter)->isSelectable()){
            auto oscAddress = ofSplitString((*parameter)->getOscAddress(), "/");
            int i = 0;
            while(oscAddress[i]!="opacity" && i < oscAddress.size()){
                i++;
            }

            string subpageName = oscAddress[i-1];
            

            previousPage = currentPage;
            
            std::list<MadParameterPage>::iterator pageIt;
            for(pageIt = madOscQuery.subPages.begin(); pageIt != madOscQuery.subPages.end(); pageIt++){
                if(pageIt->getName()==subpageName){
                    MadParameterPage* prevPage = &(*currentPage);
                    currentPage = pageIt;
                    setActivePage(&(*currentPage), prevPage);
                    return;
                }
            }

            oscSelectSurface(subpageName);
        }
}

void ofApp::selectGroupContent(string & name){
    // Find the name of the corresponding surface
    auto result = ofSplitString(name, "_");
    int index = ofToInt(result[1]);
    
    if(currentPage->getParameters()->size()<index) return; // catch if no parameter connected
    std::list<MadParameter*>::iterator parameter = currentPage->getParameters()->begin();
    std::advance(parameter, index-1);
    if((*parameter)->isSelectable() && (*parameter)->isGroup()){
        auto oscAddress = ofSplitString((*parameter)->getOscAddress(), "/");
        string subpageName = oscAddress[2];
        
        if((*currentPage).isSubpage()) return;
        previousPage = currentPage;
        
        std::list<MadParameterPage>::iterator pageIt;
        for(pageIt = madOscQuery.subPages.begin(); pageIt != madOscQuery.subPages.end(); pageIt++){
            if(pageIt->getName()==subpageName+"_SubPage"){
                MadParameterPage* prevPage = &(*currentPage);
                currentPage = pageIt;
                setActivePage(&(*currentPage), prevPage);
                return;
            }
        }
        
        oscSelectSurface(subpageName);
    }
}

void ofApp::selectMedia(string & name){
    if((*currentPage).isSubpage()) return;
    
    // Find the name of the corresponding surface
    auto result = ofSplitString(name, "_");
    int index = ofToInt(result[1]);
    if(currentPage->getParameters()->size()<index) return; // catch if no parameter connected
    std::list<MadParameter*>::iterator parameter = currentPage->getParameters()->begin();
    std::advance(parameter, index-1);
    if((*parameter)->isSelectable()){
        string subpageName = (*parameter)->getConnectedMediaName();
        
        
        previousPage = currentPage;
        
        std::list<MadParameterPage>::iterator pageIt;
        for(pageIt = madOscQuery.subPages.begin(); pageIt != madOscQuery.subPages.end(); pageIt++){
            if(pageIt->getName()==subpageName){
                MadParameterPage* prevPage = &(*currentPage);
                currentPage = pageIt;
                setActivePage(&(*currentPage), prevPage);
                return;
            }
        }
        
        oscSelectSurface(subpageName);
    }
}
void ofApp::showMedia(string & name){
    ofRemoveListener(madOscQuery.mediaNameE, this, &ofApp::showMedia);

    if((*currentPage).isSubpage()) return;
    previousPage = currentPage;
    
    std::list<MadParameterPage>::iterator pageIt;
    for(pageIt = madOscQuery.subPages.begin(); pageIt != madOscQuery.subPages.end(); pageIt++){
        if(pageIt->getName()==name){
            MadParameterPage* prevPage = &(*currentPage);
            currentPage = pageIt;
            setActivePage(&(*currentPage), prevPage);
            return;
        }
    }
    oscSelectSurface(name);
}


void ofApp::backToCurrent(float & p){
	MadParameterPage* prevPage = &(*currentPage);
    currentPage = previousPage;
	setActivePage(&(*currentPage), prevPage);
    
    for(auto & midiComponent : selectGroup.midiComponents){
        midiComponent.second->value.disableEvents();
        midiComponent.second->value = 0;
        midiComponent.second->update();
        midiComponent.second->value.enableEvents();
    }
}

void ofApp::chanForward(float & p){
    if(p == 1){
        (*currentPage).cycleForward();
    }
    updateParameterDisplay();
}

void ofApp::chanBackward(float & p){
    if(p == 1){
        (*currentPage).cycleBackward();
    }
    updateParameterDisplay();
}

void ofApp::bankForward(float & p){
	if(next(currentPage) != madOscQuery.pages.end() && p == 1){
		MadParameterPage* prevPage = &(*currentPage);
		currentPage++;
		setActivePage(&(*currentPage), prevPage);
	}
}

void ofApp::bankBackward(float & p){
	if(currentPage != madOscQuery.pages.begin() && p == 1){
		MadParameterPage* prevPage = &(*currentPage);
		currentPage--;
		setActivePage(&(*currentPage), prevPage);
	}
}

void ofApp::reload(float & p){
    if(p==1){
        isLoading = true;
        auto success = reloadFromServer(p);
        madMapperLoadError = !success;
    }else isLoading = false;
}

void ofApp::removeListeners(){
	
	currentPage->unlinkDevice();
	
    platformM.midiComponents["chan_up"].value.removeListener(this, &ofApp::chanForward);
    platformM.midiComponents["chan_down"].value.removeListener(this, &ofApp::chanBackward);
	platformM.midiComponents["bank_up"].value.removeListener(this, &ofApp::bankForward);
	platformM.midiComponents["bank_down"].value.removeListener(this, &ofApp::bankBackward);
    platformM.midiComponents["rep"].value.removeListener(this, &ofApp::reload);
    platformM.midiComponents["mixer"].value.removeListener(this, &ofApp::backToCurrent);
	
	fadeToBlack->unlinkMidiComponent(platformM.midiComponents["fader_M"]);
    speed->unlinkMidiComponent(platformM.midiComponents["jog"]);

    ofRemoveListener(selectGroup.lastChangedE, this, &ofApp::selectSurface);
    ofRemoveListener(selectGroup.noneSelectedE, this, &ofApp::backToCurrent);
    ofRemoveListener(recGroup.lastChangedE, this, &ofApp::selectSurface);
    ofRemoveListener(recGroup.noneSelectedE, this, &ofApp::backToCurrent);
    ofRemoveListener(soloGroup.lastChangedE, this, &ofApp::selectMedia);
    ofRemoveListener(soloGroup.noneSelectedE, this, &ofApp::backToCurrent);
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

void ofApp::oscSelectMedia(string name){
    string oscAddress = "/medias/select_by_name";
    
    ofxOscMessage m;
    m.setAddress(oscAddress);
    m.addStringArg(name);
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
    if(!isLoading){
        if(!madMapperLoadError){
            ofSetWindowTitle("MADMAPPER MIDI MAPPER");
        }else{
            ofSetWindowTitle("MADMAPPER LOAD ERROR");
        }
    }
}

void ofApp::setupPages(ofJson madmapperJson){
    
//    // Custom
//    madOscQuery.createCustomPage(pages, &platformM, ofLoadJson("custom_page.json"));
//
//    // One for each possible Subpage
//    madOscQuery.createSubPages(subPages, &platformM, madmapperJson);
    
//    cout << "subPages " << subPages.size() << endl;
    
	// Set initial page
	currentPage = madOscQuery.pages.begin();
    previousPage = currentPage;
    
	setActivePage(&(*currentPage), nullptr);
}
    
void ofApp::setupUI(ofJson madmapperJson){
	
	// Setup all the specialised control.
    platformM.midiComponents["chan_up"].value.addListener(this, &ofApp::chanForward);
    platformM.midiComponents["chan_down"].value.addListener(this, &ofApp::chanBackward);
    platformM.midiComponents["bank_up"].value.addListener(this, &ofApp::bankForward);
	platformM.midiComponents["bank_down"].value.addListener(this, &ofApp::bankBackward);
	platformM.midiComponents["mixer"].value.addListener(this, &ofApp::backToCurrent);
    platformM.midiComponents["rep"].value.addListener(this, &ofApp::reload);
	
	// Fixed Controlls
//    fadeToBlack = madOscQuery.createParameter(madmapperJson["CONTENTS"]["master"]["CONTENTS"]["fade_to_black"]);
    fadeToBlack = madOscQuery.createParameter(madmapperJson["CONTENTS"]["surfaces"]["CONTENTS"]["Mapping"]["CONTENTS"]["opacity"]);

    fadeToBlack->linkMidiComponent(platformM.midiComponents["fader_M"]);

    speed = madOscQuery.createParameter(madmapperJson["CONTENTS"]["master"]["CONTENTS"]["GlobalBPM"]["CONTENTS"]["BPM"]); // MadMapper 3.5
    speed->linkMidiComponent(platformM.midiComponents["jog"]);
	// Select Group.
	selectGroup.doCheckbox = true;
	for(int i = 1; i<9; i++){
		selectGroup.add(platformM.midiComponents["sel_"+ofToString(i)]);
	}
	ofAddListener(selectGroup.lastChangedE, this, &ofApp::selectSurface);
	ofAddListener(selectGroup.noneSelectedE, this, &ofApp::backToCurrent);
    
    // Rec Group.
    recGroup.doCheckbox = false;
    for(int i = 1; i<9; i++){
        recGroup.add(platformM.midiComponents["rec_"+ofToString(i)]);
    }
    ofAddListener(recGroup.lastChangedE, this, &ofApp::selectSurface);
    ofAddListener(recGroup.noneSelectedE, this, &ofApp::backToCurrent);
    
    // Rec Group.
    soloGroup.doCheckbox = true;
    for(int i = 1; i<9; i++){
        soloGroup.add(platformM.midiComponents["solo_"+ofToString(i)]);
    }
    ofAddListener(soloGroup.lastChangedE, this, &ofApp::selectMedia);
    ofAddListener(soloGroup.noneSelectedE, this, &ofApp::backToCurrent);
}

//--------------------------------------------------------------
void ofApp::draw(){
    if(!isLoading){

	if(!madMapperLoadError){
		ofBackground(0);
		drawStatusString();
	}else{
		std::string s = "MADMAPPER HTTP ENDPOINT NOT FOUND - TRY AGAIN!";
		ofDrawBitmapStringHighlight(s, 15, 15);
		ofTranslate(ofGetWidth()/2, ofGetHeight()/2);
		errorImage.draw(-errorImage.getWidth()/2,-errorImage.getHeight()/2,errorImage.getWidth(), errorImage.getHeight());
	}
        if(showMidiIn){
            platformM.gui.setPosition(10,10);
            platformM.gui.draw();
        }

    }
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
	float p = 1; // for use in callback functions
	
	if(key == 's'){
		//        platformM.saveMidiComponentsToFile("platformM.json");
	}
    if(key == 'm'){
        showMidiIn=!showMidiIn;
        
    }
    
	
	if(key == ' '){
//        auto success = reloadFromServer(p);
//        madMapperLoadError = !success;
        madOscQuery.updateValues();
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
		chanBackward(p);
	}
	if(key == OF_KEY_RIGHT && !madMapperLoadError){
		chanForward(p);
	}
    
    if(key>48 && key<58){
        int index = key-48;
        string name = "solo_"+ofToString(index);
        selectMedia(name);
    }
    if(key == '0'){
        float p = 1;
        backToCurrent(p);
    }
}


//--------------------------------------------------------------
void ofApp::setActivePage(MadParameterPage* page, MadParameterPage* prevPage){
	if(prevPage != nullptr){
		prevPage->unlinkDevice();
	}
	page->linkDevice();
	
	ofLog() << "Active page set to " << (*currentPage).getName() << endl;
    
    updatePageDisplay();
    updateParameterDisplay();

}

//--------------------------------------------------------------
void ofApp::drawStatusString(){
	std::string s = "";
	s+= "Current page: " + (*currentPage).getName();
	s+= "\nRange: " + ofToString((*currentPage).getRange().first) + " " + ofToString((*currentPage).getRange().second);
	s+= "\nParameters on page:";
	
	int parNum = 1;
	for(auto& p : *(*currentPage).getParameters()){
        s+= "\n";
        if(parNum>=(*currentPage).getRange().first && parNum<=(*currentPage).getRange().second) s+= "* ";
        else s+= "  ";
        s+= ofToString(parNum) + ") ";
        if(p->isGroup())s+= "G ";
        s+= p->oscAddress + " " + ofToString(p->getParameterValue());
		parNum++;
	}
	ofDrawBitmapString(s, 15, 15);
    
}
//--------------------------------------------------------------
bool ofApp::reloadFromServer(float & p){
    if(p == 1){
	ofLog(OF_LOG_NOTICE) << "Attempting reload from server" << endl;
	// Save previous location
	std::string prevPageName = "";
	int prevLowerBound = 1;
	if(initialised){
		std::string prevPageName = (*currentPage).getName();
		int prevLowerBound = (*currentPage).getRange().first;
	}
        madOscQuery.receive();
        
	// Reloads parameters from MadMapper
	if(madOscQuery.madMapperJson == nullptr){
	ofLog(OF_LOG_WARNING) << "Reload unsuccessful!" << endl;
		return false;
	}
        madOscQuery.createCustomPages(&platformM, ofLoadJson("custom_page.json"), madOscQuery.madMapperJson);

	// Setups pages
	if(initialised){
		removeListeners();
		selectGroup.clear();
        recGroup.clear();
        soloGroup.clear();
		madOscQuery.pages.clear();
	}

//    setupPages(madmapperJson);
    setupUI(madOscQuery.madMapperJson);

	std::list<MadParameterPage>::iterator pageIt;
	for(pageIt = madOscQuery.pages.begin(); pageIt != madOscQuery.pages.end(); pageIt++){
		if(pageIt->getName() == prevPageName){
			MadParameterPage* prevPage = &(*currentPage);
			currentPage = pageIt;
			setActivePage(&(*currentPage), nullptr);
			(*currentPage).setLowerBound(prevLowerBound);
			ofLog(OF_LOG_NOTICE) << "Reload successfully and resuming from " << prevPageName << endl;
			return true;
		}
	}
	ofLog(OF_LOG_NOTICE) << "Reload successful assigning to first page!" << endl;
	currentPage = madOscQuery.pages.begin();
	setActivePage(&(*currentPage), nullptr);
	return true;
    } else return false;
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

void ofApp::updatePageDisplay(){
    // UPPER ROW_INPUT
    vector<unsigned char> text;
    text.push_back(0xF0);
    text.push_back(0x00);
    text.push_back(0x00);
    text.push_back(0x66);
    text.push_back(0x14); // DEVICE ID
    text.push_back(0x12); // DISPLAY
    //    text.push_back(0x00); // POSITION FIRST LINE
    text.push_back(0x38); // UPPER LINE
    
    std::string str = (*currentPage).getName();;
    std::copy(str.begin(), str.end(), std::back_inserter(text));

    while(text.size()<66+6){
        text.push_back(' '); // Clear other fields
    }
    
    text.push_back(0xF7);// DETERMINATOR
    platformM.midiOut.sendMidiBytes(text);
}

void ofApp::updateParameterDisplay(){
    // UPPER ROW_INPUT
    vector<unsigned char> text;
    text.push_back(0xF0);
    text.push_back(0x00);
    text.push_back(0x00);
    text.push_back(0x66);
    text.push_back(0x14); // DEVICE ID
    text.push_back(0x12); // DISPLAY
    text.push_back(0x00); // POSITION FIRST LINE
//    text.push_back(0x38); // UPPER LINE
//    text.push_back('|');

    int parNum = 1;
    for(auto& p : *(*currentPage).getParameters()){
        if(parNum>=(*currentPage).getRange().first && parNum<=(*currentPage).getRange().second){
            string oscAddress = p->oscAddress;
            auto result = ofSplitString(oscAddress, "/");
            string page = result[result.size()-2];
            
            int maxNumChar = 6;
            string name;
            if(p->isMaster) name = result[result.size()-2];
//            if(!(*currentPage).isSubpage()) name = result[result.size()-1];
            else name = result[result.size()-1];
            
            int length = name.size();
            if(name.size()>maxNumChar) length = maxNumChar;
            std::copy(name.begin(), name.begin()+length, std::back_inserter(text));
            for(int i=length;i<maxNumChar;i++) text.push_back(' ');
            text.push_back(' ');
        }
        parNum++;
    }

    while(text.size()<66+6){
        text.push_back(' '); // Clear other fields
    }
    
    text.push_back(0xF7);// DETERMINATOR
    platformM.midiOut.sendMidiBytes(text);
}

