/*
A page contains a list of MadParameters
*/


class Page{
public:
	Page(std::string name, ofxMidiDevice* midiDevice){
		this->name = name;
		this->midiDevice = midiDevice;
	};

	void addParameter(MadParameter parameter){
		parameters.push_back(parameter);
		int upper = parameters.size(); // set range
		
		if(upper > 8){
			// 8 is max size
			upper = 8;
		}
		
		range = std::make_pair(0, upper);
	}
	
	bool isEmpty(){
		return parameters.size() == 0;
	}
	
	std::string getName(){
		return this->name;
	}
	
	std::list<MadParameter>* getParameters(){
		return &this->parameters;
	}
	
	void cycleForward(){
		if(range.second < parameters.size()){
    		ofLog() << "Cycling forwards - new range: " << range.first << " to " << range.second;
			range.first++;
			range.second++;
		}
	}
	
	void cycleBackward(){
		if(range.first > 0){
    		ofLog() << "Cycling backwards - new range: " << range.first << " to " << range.second;
			range.first--;
			range.second--;
		}
	}
	
	std::pair<int,int> getRange(){
		return range;
	}
	
	void removeListeners(MidiComponent &midiComponent){
		for(auto& p : parameters){
			p.unlinkMidiComponent(midiComponent);
		}
	}
	
	void linkDevice(){
    	auto parameter = parameters.begin();
    	for(int i = 1; i < 9 && (parameter != parameters.end()); i++){
    		parameter->linkMidiComponent(midiDevice->midiComponents["fader_" + ofToString(i)]);
    		parameter++;
    	}
	}
	
	void unlinkDevice(){
		auto prevParameter = parameters.begin();
		for(int i = 1; i < 9 && (prevParameter != parameters.end()); i++){
			prevParameter->unlinkMidiComponent(midiDevice->midiComponents["fader_" + ofToString(i)]);
			prevParameter++;
		}
	}

private:
	std::list<MadParameter> parameters;
	std::string name = "";
	std::pair<int, int> range;
	ofxMidiDevice* midiDevice;
};
