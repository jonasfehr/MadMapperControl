#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup(){
	madOscQuery.setup("127.0.0.1", 8012, 8011);
	madOscQuery.receive();
	
	//    platformM.setupFromFile("platformM.json");
	//    platformM.setupFromFile("");
	platformM.setup("Platform M+ V1.07");
	
	//    ofxMidiIn::listPorts();
	//    platformM.setup("Platform M+ V1.06");
	//    for(int i = 0; i < madOscQuery.mixer.parameters.size(); i++){
	//        platformM.addFader(madOscQuery.mixer.parameters.at(i), i+1);
	//
	//    }
	
	// for each ?
	
	// All Opacities
	auto fileName = "madMapperExampleFX.json";
	ofFile jsonFile(fileName);
	ofJson madmapperJson = ofLoadJson(jsonFile);
	
	createOpacityPages(madmapperJson);
	//	std::cout << pages.begin()->parameters.size() << endl;
	
	// One for each Surface
	std::vector<string> fx = {};
	createSurfacePages(madmapperJson, fx);
	
	// One for each Media
	
	
	// Set initial page
	currentPage = pages.begin();
	setActivePage(&(*currentPage));
	
}

//--------------------------------------------------------------
void ofApp::update(){
	//cout << surfaces.size() << endl;
	madOscQuery.update();
	platformM.update();
	
	//	platformM.midiComponents["fader_1"].value = pages.begin()->parameters.begin()->getParameterValue();
	
	ofSetWindowTitle((*currentPage).getName());
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
	platformM.gui.setPosition(230,10);
	platformM.gui.draw();
	
	
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
	if(key == 's'){
		//		platformM.saveMidiComponentsToFile("platformM.json");
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
			currentPage++;
			setActivePage(&(*currentPage));
		}
	}
	if(key == OF_KEY_DOWN){
		if(currentPage != pages.begin()){
			currentPage--;
			setActivePage(&(*currentPage));
		}
	}
}

//--------------------------------------------------------------
void ofApp::createOpacityPages(ofJson json){
	// Create pages for opacity value for each surface
	auto keyword = "opacity";
	Page page = Page(keyword);
	for(auto & element : json["CONTENTS"]["surfaces"]["CONTENTS"]){
		if(element["DESCRIPTION"] == "selected"){
			// Skip this one
			continue;
		}
		// Add element
		page.addParameter(MadParameter(element["CONTENTS"][keyword]));
		
		// Max 8 params per page
		if(page.isFull()){
			pages.push_back(page);
			page = Page(keyword);
		}
	}
	
	if(!page.isEmpty()){
		pages.push_back(page);
	}
}

//--------------------------------------------------------------
void ofApp::createSurfacePages(ofJson json, std::vector<string> fx){
	auto keyword = "surface";
	for(auto & element : json["CONTENTS"]["surfaces"]["CONTENTS"]){
		Page page = Page(keyword);
		
		// Add parameters
		for(auto& color : element["CONTENTS"]["color"]["CONTENTS"]){
			// Add rgb
			if(color["DESCRIPTION"] == "Red" || color["DESCRIPTION"] == "Green" || color["DESCRIPTION"] == "Blue"){
				page.addParameter(MadParameter(color));
			}
			
			// TODO: Add FX
			
			
			// Max 8 params per page
			if(page.isFull()){
				pages.push_back(page);
				page = Page(keyword);
			}
			
		}
		if(!page.isEmpty()){
			pages.push_back(page);
		}
	}
	
	if(fx.size() > 0){
		// Add remaining parameters
	}
}
//--------------------------------------------------------------
void ofApp::setActivePage(Page* page){
	// Update fader control to fit input page
	auto parameter = page->getParameters()->begin();
	for(int i = 1; i < 9 && (parameter != page->getParameters()->end()); i++){
		parameter->set(platformM.midiComponents["fader_" + ofToString(i)].value);
		parameter++;
	}
	ofLog() << "Active page set to " << (*currentPage).getName() << endl;
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

