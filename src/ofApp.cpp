#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup(){
    madOscQuery.setup("127.0.0.1", 8012, 8011);
    ofJson madmapperJson = madOscQuery.receive();
    
    //    platformM.setupFromFile("platformM.json");
    //    platformM.setupFromFile("");
    platformM.setup("Platform M+ V2.00");
    launchpad.setup("Launchpad");
    // JSON file
    //    auto fileName = "madMapperExampleFX.json";
    //    ofFile jsonFile(fileName);
    //    ofJson madmapperJson = ofLoadJson(jsonFile);
    
    // Create opacity pages
    createOpacityPages(madmapperJson);
    
    // One for each Surface
    std::vector<string> fx = {};
    createSurfacePages(madmapperJson, fx);
    
    // One for each Media
    
    // Set initial page
    currentPage = pages.begin();
    setActivePage(&(*currentPage), nullptr);
    
    // Add callback for parameter change in midi controller
    //    ofAddListener(platformM.parameterGroup.parameterChangedE(), this, &ofApp::exit);
    //    ofAddListener(platformM.parameterGroup.parameterChangedE(), this, &ofApp::listenerFunction);
    //    ofAddListener(MadEvent::events, this, &ofApp::madParameterEvent);
}

//--------------------------------------------------------------
void ofApp::update(){
    //cout << surfaces.size() << endl;
    madOscQuery.update();
    platformM.update();
    
    // for each midi parameter
    //    for(auto& mp : platformM.)
    // if updated
    // throw MadEvent
    
    //    platformM.midiComponents["fader_1"].value = pages.begin()->parameters.begin()->getParameterValue();
    ofSetWindowTitle((*currentPage).getName());
}

//--------------------------------------------------------------
void ofApp::listenerFunction(ofAbstractParameter& e){
    // Must be in ofApp
    std::cout << e.getName() << endl;
}

//--------------------------------------------------------------
void ofApp::madParameterEvent(MadEvent &e){
    std::cout << e.oscAddress << endl;
}

//--------------------------------------------------------------
void ofApp::draw(){
    //    std::stringstream ss;
    //    for(auto & s : madOscQuery.surfaces){
    //        ss << s.second.description << endl;
    //        ss << " -> "<< s.second.connectedMedia << endl;
    //        for(auto & p : madOscQuery.medias[s.second.connectedMedia].parameters){
    //            ss << "    -> " << p.getName() << endl;
    //        }
    //
    //
    //    }
    //    ofDrawBitmapString(ss.str(), 20, 20);
    //    ss.str("");
    
    //    madOscQuery.draw();
    
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
            std::cout << p.getParameterValue() << endl;
        }
    }
    
    if(key == OF_KEY_UP){
        if(next(currentPage) != pages.end()){
            Page* prevPage = &(*currentPage);
            currentPage++;
            setActivePage(&(*currentPage), prevPage);
        }
    }
    if(key == OF_KEY_DOWN){
        if(currentPage != pages.begin()){
            Page* prevPage = &(*currentPage);
            currentPage--;
            setActivePage(&(*currentPage), prevPage);
        }
    }
    
    // Cycle through current page
    if(key == OF_KEY_LEFT){
        (*currentPage).cycleBackward();
    }
    if(key == OF_KEY_RIGHT){
        (*currentPage).cycleForward();
    }
}

//--------------------------------------------------------------
void ofApp::createOpacityPages(ofJson json){
    // Create pages for opacity value for each surface
	std::string keyword = "opacity";
    Page page = Page(keyword, &platformM);
    for(auto & element : json["CONTENTS"]["surfaces"]["CONTENTS"]){
        if(element["DESCRIPTION"] == "selected"){
            // Skip this one
            continue;
        }
        // Add element
        page.addParameter(MadParameter(element["CONTENTS"][keyword],element["DESCRIPTION"]));
    }
    
    if(!page.isEmpty()){
        pages.push_back(page);
    }
}

//--------------------------------------------------------------
void ofApp::createSurfacePages(ofJson json, std::vector<string> fx){
    std::string name = "surface";
    int idx = 0;
    for(auto & element : json["CONTENTS"]["surfaces"]["CONTENTS"]){
        if(element["DESCRIPTION"] == "selected"){
            continue; // skip this one
        }
        auto keyword = element["DESCRIPTION"].get<std::string>();//name + "_" + ofToString(idx);
        Page page = Page(name, &platformM);
        
        // Add parameters
        
        
        for(auto& contents : element["CONTENTS"]){
            if(contents["DESCRIPTION"] == "Opacity"){
                page.addParameter(MadParameter(contents));
            }
            if(contents["DESCRIPTION"] == "Color"){
                for(auto& color : contents["CONTENTS"]){
                    // Add rgb
                    if(color["DESCRIPTION"] == "Red" || color["DESCRIPTION"] == "Green" || color["DESCRIPTION"] == "Blue"){
                        page.addParameter(MadParameter(color));
                    }
                }
            }
            
            if(contents["DESCRIPTION"] == "fx"){
                for(auto& fx : contents["CONTENTS"]){
                    // Add rgb
                    if( fx["DESCRIPTION"] != "FX Type" && fx["TYPE"]=="f"){
                        page.addParameter(MadParameter(fx));
                    }
                    
                    // TODO: Add FX
                    
                    // Max 8 params per page
                }
            }
        }
        
        
        if(!page.isEmpty()){
            pages.push_back(page);
            idx++;
        }
    }
    
    if(fx.size() > 0){
        // Add remaining parameters
    }
}
//--------------------------------------------------------------
void ofApp::setActivePage(Page* page, Page* prevPage){
    if(prevPage != nullptr){
    	prevPage->unlinkDevice();
    }

    // Update fader control to fit input page
    // Add new listeners
    auto parameter = page->getParameters()->begin();
    for(int i = 1; i < 9 && (parameter != page->getParameters()->end()); i++){
        parameter->linkMidiComponent(platformM.midiComponents["fader_" + ofToString(i)]);
        parameter++;
    }
    
    ofLog() << "Active page set to " << (*currentPage).getName() << endl;
}

std::string ofApp::getStatusString(){
    std::string s = "";
    s+= "Current page: " + (*currentPage).getName();
    s+= "\nRange: " + ofToString((*currentPage).getRange().first) + " " + ofToString((*currentPage).getRange().second);
    s+= "\nParameters on page:";
    
    int parNum = 1;
    for(auto& p : *(*currentPage).getParameters()){
        s+= "\n" + ofToString(parNum) + ") " + p.oscAddress + " " + ofToString(p.getParameterValue());
        parNum++;
    }
    
    return s;
}
//--------------------------------------------------------------
void ofApp::exit(){
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



