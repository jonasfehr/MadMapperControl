/*
A page contains a list of MadParameters
*/


class Page{
public:
	Page(std::string name){
		this->name = name;
	};
	
	void addParameter(MadParameter parameter){
		if(parameters.size() >= 8){
			throw std::invalid_argument("A page cannot contain this many parameters!!");
		}
		parameters.push_back(parameter);
	}
	
	bool isFull(){
		// Cannot contain more parameters
		return parameters.size() >= 8;
	}
	
	bool isEmpty(){
		// Contains no parameters
		return parameters.size() == 0;
	}
	
	std::string getName(){
		return this->name;
	}
	

//private:
	std::list<MadParameter> parameters;
	std::string name = "";
};
