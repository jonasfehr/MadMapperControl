#pragma once

#include "ofMain.h"
#include "ofxMadOscQuery.h"
#include "ofxMidiDevice.h"
#include "Page.hpp"
#include "MadParameterPage.hpp"

class ofApp : public ofBaseApp{
	
public:
	void setup();
	void update();
	void draw();
	void exit();
	
	void keyPressed(int key);
	void keyReleased(int key);
	void mouseMoved(int x, int y );
	void mouseDragged(int x, int y, int button);
	void mousePressed(int x, int y, int button);
	void mouseReleased(int x, int y, int button);
	void mouseEntered(int x, int y);
	void mouseExited(int x, int y);
	void windowResized(int w, int h);
	void dragEvent(ofDragInfo dragInfo);
	void gotMessage(ofMessage msg);
	
	std::list<MadParameterPage> pages;
	std::list<MadParameterPage>::iterator currentPage;
	
	ofxMadOscQuery madOscQuery;
    ofxMidiDevice platformM;
    ofxMidiDevice launchpad;

	void createOpacityPages(ofJson json);
	void createSurfacePages(ofJson json, std::vector<string> fx);
	void createMediaPages(ofJson json);

	void setActivePage(MadParameterPage* page, MadParameterPage* prevPage);
    std::string getStatusString();
    
    void oscSendToMadMapper(ofxOscMessage &m);
};
