#include "ofMain.h"
#include "ofApp.h"
#include "ofAppNoWindow.h"

int main() {
#ifdef TARGET_LINUX
    ofSetupOpenGL(new ofAppNoWindow(), OF_WINDOW);
#else
    ofSetupOpenGL(420, 600, OF_WINDOW);
#endif
    ofRunApp(new ofApp());
}
