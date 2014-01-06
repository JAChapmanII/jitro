#include "config.hpp"
using std::map;
using std::string;

#include <fstream>
using std::ifstream;

#include <iostream>
using std::cerr;
using std::endl;

#include "util.hpp"
using util::trim;

int Config::load(string fileName) {
	ifstream in(fileName);
	if(!in.good())
		return -1;

	string scope = _defaultScope, line;
	while(in.good() && !in.eof()) {
		getline(in, line);

		if(in.eof() || !in.good())
			break;

		line = trim(line);
		// empty or comment lines
		if(line.empty() || line[0] == '#')
			continue;

		if(line[0] == '[') {
			scope = line.substr(1, line.length() - 2);
		} else if(line.find('=') != string::npos) {
			string variable = trim(line.substr(0, line.find('='))),
					value = trim(line.substr(line.find('=') + 1));
			set(scope, variable, value);
		} else {
			cerr << "Config::load: unknown line: " << line << endl;
		}
	}

	return _map.size();
}
void Config::clear() {
	_map.clear();
}

string Config::defaultScope() const {
	return _defaultScope;
}
void Config::defaultScope(string scope) {
	_defaultScope = scope;
}

bool Config::has(string scopedVariable) {
	return (_map.find(scopedVariable) != _map.end());
}
bool Config::has(string scope, string variable) {
	return has(Config::scopeVariable(scope, variable));
}

string Config::get(string scopedVariable) {
	return _map[scopedVariable];
}
string Config::get(string scope, string variable) {
	return get(Config::scopeVariable(scope,  variable));
}

void Config::set(string scopedVariable, string value) {
	_map[scopedVariable] = value;
}
void Config::set(string scope, string variable, string value) {
	set(Config::scopeVariable(scope, variable), value);
}

string &Config::operator[](string scopedVariable) {
	return _map[scopedVariable];
}

// TODO: used? leaky
map<string, string>::iterator Config::begin() {
	return _map.begin();
}
map<string, string>::iterator Config::end() {
	return _map.end();
}

string Config::scopeVariable(string scope, string variable) {
	return scope + "." + variable;
}

