#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup(){
    // Setup midi controllers
    platformM.setup("Platform M+ V2.00");
    launchpad.setup("Launchpad");
    
    
    madOscQuery.setup("127.0.0.1", 8010, 8011);
    ofJson madmapperJson = madOscQuery.receive();
    
    // Create opacity pages
    madOscQuery.createOpacityPages(pages, &platformM, madmapperJson);
    
    // One for each Surface
    madOscQuery.createSurfacePages(pages, &platformM, madmapperJson);
    
    // Set initial page
    currentPage = pages.begin();
    setActivePage(&(*currentPage), nullptr);
    
    // Setup all the specialised control.
    for(auto & page : pages) page.linkCycleControlComponents(platformM.midiComponents["chan_up"], platformM.midiComponents["chan_down"]);
    
}

//--------------------------------------------------------------
void ofApp::update(){
    ofSetWindowTitle((*currentPage).getName());
}

//--------------------------------------------------------------
void ofApp::draw(){
    
    //    platformM.drawRawInput();
    platformM.gui.setPosition(ofGetWidth()-230,10);
    platformM.gui.draw();
    
    launchpad.gui.setPosition(ofGetWidth()-460,10);
    launchpad.gui.draw();
    
    ofDrawBitmapStringHighlight(getStatusString(), 15, 15);
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
    if(key == 's'){
        //        platformM.saveMidiComponentsToFile("platformM.json");
    }
    
    if(key == 'l'){
        platformM.setupFromFile("platformM.json");
    }
    
    if(key == 'o'){
        for(auto& p : *(*currentPage).getParameters()){
            std::cout << p->getParameterValue() << endl;
        }
    }
    
    if(key == OF_KEY_UP){
        if(next(currentPage) != pages.end()){
            MadParameterPage* prevPage = &(*currentPage);
            currentPage++;
            setActivePage(&(*currentPage), prevPage);
        }
    }
    if(key == OF_KEY_DOWN){
        if(currentPage != pages.begin()){
            MadParameterPage* prevPage = &(*currentPage);
            currentPage--;
            setActivePage(&(*currentPage), prevPage);
        }
    }
    
    // Cycle through current page
    if(key == OF_KEY_LEFT){
        float p = 1;
        (*currentPage).cycleBackward(p);
    }
    if(key == OF_KEY_RIGHT){
        float p = 1;
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
void ofApp::exit(){
    for(auto & page : pages) page.unlinkCycleControlComponents(platformM.midiComponents["chan_up"], platformM.midiComponents["chan_down"]);
    
    currentPage->unlinkDevice();

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




