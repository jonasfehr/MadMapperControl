#include "ofMain.h"
#include "ofApp.h"

//========================================================================
int main( ){
	ofSetupOpenGL(420,600,OF_WINDOW);			// <-------- setup the GL context
	ofRunApp(new ofApp());
}
