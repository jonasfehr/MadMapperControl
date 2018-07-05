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
	
	ofxMadOscQuery madOscQuery;
    ofxMidiDevice platformM;

    std::list<MadParameterPage> pages;
    std::list<MadParameterPage> subPages;
    std::list<MadParameterPage>::iterator currentPage;
    std::list<MadParameterPage>::iterator previousPage;

	void setActivePage(MadParameterPage* page, MadParameterPage* prevPage);
	void setupPages(ofJson madMapperJson);
    void setupUI(ofJson madMapperJson);
    void drawStatusString();
	bool madMapperLoadError = true;
	bool initialised = false;
	ofImage errorImage;

    // FOR UX
    void updatePageDisplay();
    void updateParameterDisplay();
    void chanForward(float & p);
    void chanBackward(float & p);
    void bankForward(float & p);
    void bankBackward(float & p);
    MadParameter * fadeToBlack;
    MadParameter * speed;
    MidiComponentGroup selectGroup;
    MidiComponentGroup muteGroup; // not used yet
    MidiComponentGroup soloGroup; // not used yet
    MidiComponentGroup recGroup;
    void selectSurface(string & name);
    void selectGroupContent(string & name);
    void selectMedia(string & name);
    void showMedia(string & name);
    void backToCurrent(float & p);
    bool reloadFromServer(float & p);
    void reload(float & p);
    bool isLoading;
    bool showMidiIn = false;
    
    // OSC functions
    void oscSelectSurface(string name);
    void oscRequestMediaName();

    void removeListeners();

};
