/*
 A page contains a list of MadParameters
 */

#include "ofApp.h"

class Page{
public:
    Page(std::string name){
        this->name = name;
    };
    
    void addParameter(MadParameter parameter){
        if(parameters.size() > 8){
            throw std::invalid_argument("A page cannot contain this many parameters!!");
        }
        parameters.push_front(parameter);
        
        // Set range
        int upper = parameters.size();
        
        if(upper > 8){
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
    
    void cycleForward(float & p){
        if(p == 1){
            if(range.second < parameters.size()){
                ofLog() << "Cycling forwards - new range: " << range.first << " to " << range.second;
                range.first++;
                range.second++;
            }
        }
    }
    
    void cycleBackward(float & p){
        if(p == 1){
            if(range.first > 0){
                ofLog() << "Cycling backwards - new range: " << range.first << " to " << range.second;
                range.first--;
                range.second--;
            }
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
    
    //    void (MidiC)
    
    
    void linkCycleControlComponents(MidiComponent &midiComponentForward,MidiComponent &midiComponentBackward){
        midiComponentForward.value.addListener(this, &Page::cycleForward);
        midiComponentBackward.value.addListener(this, &Page::cycleBackward);
    }
    
    void unlinkCycleControlComponents(MidiComponent &midiComponentForward,MidiComponent &midiComponentBackward){
        midiComponentForward.value.removeListener(this, &Page::cycleForward);
        midiComponentBackward.value.removeListener(this, &Page::cycleBackward);
    }
    
private:
    std::list<MadParameter> parameters;
    std::string name = "";
    std::pair<int, int> range;
};

