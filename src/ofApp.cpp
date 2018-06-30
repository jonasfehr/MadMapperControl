#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup(){
    madOscQuery.setup("127.0.0.1", 8012, 8011);
    madOscQuery.receive();
    

    platformM.setupFromFile("platformM.json");
//    platformM.setupFromFile("platformM.json");

    //    ofxMidiIn::listPorts();
    //    platformM.setup("Platform M+ V1.06");
//    for(int i = 0; i < madOscQuery.mixer.parameters.size(); i++){
//        platformM.addFader(madOscQuery.mixer.parameters.at(i), i+1);
//
//    }
    
}

//--------------------------------------------------------------
void ofApp::update(){
    //cout << surfaces.size() << endl;
    madOscQuery.update();
    platformM.update();
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

    madOscQuery.draw();
    
//    platformM.drawRawInput();
    platformM.gui.setPosition(230,10);

    platformM.gui.draw();
    
    
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
    if(key == 's'){
        platformM.saveDeviceComponentsToFile("platformM.json");
    }
    
    if(key == 'l'){
        platformM.setupFromFile("platformM.json");
    }
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

