#pragma once

#include "ofMain.h"
#include "ofxGui.h"
#include "ofxMadOscQuery.h"
#include "ofxMidiDevice.h"
#include "ofxOscParameterSync.h"
//#include "Faderport16Surface.h"
#include "Push3Surface.h"

#define HOST "localhost"
#define PORT_RECEIVE 8010
#define PORT_FEEDBACK 9893


class writeLogToWindow : public ofBaseLoggerChannel {
public:
	/// \brief Destroy the console logger channel.
	virtual ~writeLogToWindow() {};
	void log(ofLogLevel level, const std::string & module, const std::string & message);
};

class ofApp : public ofBaseApp {
 public:
  void setup();
  void update();
  void draw();
  void exit();

  void keyPressed(int key);
  void keyReleased(int key);
  void mouseMoved(int x, int y);
  void mouseDragged(int x, int y, int button);
  void mousePressed(int x, int y, int button);
  void mouseReleased(int x, int y, int button);
  void mouseEntered(int x, int y);
  void mouseExited(int x, int y);
  void windowResized(int w, int h);
  void dragEvent(ofDragInfo dragInfo);
  void gotMessage(ofMessage msg);

  ofJson settings;
  string ip;
  int     queryPort;
	int feedbackPort;
	int sendPort;
  ofxMadOscQuery madOscQuery;
  //Faderport16Surface faderSurface;
	Push3Surface faderSurface;

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
  void pageForward(float& p);
  void pageBackward(float& p);
  void bankForward(float& p);
  void bankBackward(float& p);
	MadParameter* fadeMasterVideo;
	MadParameter* fadeMasterDMX;
	MadParameter* fadeEngingeSpeed;
  MadParameter* speed;
  MidiComponentGroup selectGroup;
  MidiComponentGroup soloGroup;
  MidiComponentGroup muteGroup;
  void selectSurface(string& name);
  void selectGroupContent(string& name);
  void selectMedia(string& name);
  void showMedia(string& name);
  void backToCurrent(float& p);
  bool reloadFromServer(float& p);
  void reload(float& p);
  void updateValues(float& p);

  bool isLoading;
  bool showMidiIn = false;
  bool showStatusString = false;

  // OSC functions
  void oscSelectSurface(string name);
  void oscSelectMedia(string name);
  void oscRequestMediaName();

  void removeListeners();

  ofxOscParameterSync oscParamSync;
  ofxPanel gui;

  vector<MadParameter> madParameters;
};
