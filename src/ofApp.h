#pragma once

#include "ofMain.h"
#include "ofxMadOscQuery.h"
#include "ofxMidiDevice.h"
#include "ofxOscParameterSync.h"
#include "ofxGui.h"

#define HOST "localhost"
#define PORT_SEND 9000
#define PORT_RECEIVE 9001

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
    void oscSelectMedia(string name);
    void oscRequestMediaName();

    void removeListeners();
    
    ofxOscParameterSync oscParamSync;
    ofxPanel gui;
    
    ofParameter<float> speedBoids{"speedBoids", 1.f, 0.0f, 1.0f};
    ofParameter<float> numBoids{"numBoids", 1.0f, 0.0f, 1.0f};
    ofParameter<float> cohesion{"cohesion", 0.5f, 0.0f, 1.0f};
    ofParameter<float> separation{"separation", 0.5f, 0.0f, 1.0f};
    ofParameter<float> align{"align", 0.5f, 0.0f, 1.0f};
    ofParameter<float> random{"random", 0.01f, 0.0f, 1.0f};
    ofParameter<float> borderAvoid{"borderAvoid", 0.01f, 0.0f, 1.0f};
    ofParameter<float> noiseSteering{"noiseSteerings", 0.01f, 0.0f, 1.0f};
    ofParameter<float> cohesionDist{"cohesionDist", 0.5f, 0.0f, 1.0f};
    ofParameter<float> separationDist{"separationDist", 0.5f, 0.0f, 1.0f};
    ofParameter<float> alignDist{"alignDist", 0.5f, 0.0f, 1.0f};
    ofParameter<bool> do2D{"do2D", true};
    
    ofParameterGroup parametersFlocking{"Flocking", numBoids, cohesion, separation, align, random, borderAvoid,noiseSteering,speedBoids, cohesionDist, separationDist, alignDist, do2D };

    
    ofParameter<float> pointScale{"pointScale", 1.0f, 0.0001f, 50.0f};
    ofParameter<float> dotSize{"dotSize", 1.0f, 0.f, 1.0f};
    ofParameter<float> feather{"feather", 1.0f, 0.f, 1.0f};
    ofParameter<float> gamma{"gamma", 1.0f, 0.000001f, 2.0f};
    ofParameter<float> transparency{"transparency", 1.0f, 0.f, 1.0f};
    ofParameter<float> trace{"trace", 1.0f, 0.f, 1.0f};

    
    ofParameterGroup parametersVisual{"Visual", pointScale, dotSize, feather, gamma,transparency,trace};
    
    ofParameterGroup swarmParameters{"ParticleSystem", parametersFlocking, parametersVisual};
    
    
    vector<MadParameter> madParameters;

};
