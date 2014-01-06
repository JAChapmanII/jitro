#include "util.hpp"
using std::string;
using std::vector;

using std::chrono::high_resolution_clock;
using std::chrono::system_clock;
using std::chrono::time_point;
using std::time_t;

#include <unistd.h>

string util::trim(string str, string of) {
	if(str.find_first_not_of(of) == string::npos)
		return "";
	str = str.substr(str.find_first_not_of(of));
	return str.substr(0, str.find_last_not_of(of) + 1);
}

bool util::startsWith(string str, string beg) {
	if(beg.length() > str.length())
		return false;
	return (str.substr(0, beg.length()) == beg);
}
bool util::endsWith(string str, string end) {
	if(end.length() > str.length())
		return false;
	return (str.substr(str.length() - end.length()) == end);
}

vector<string> util::split(string str, string on) {
	vector<string> fields;
	if(on.empty())
		return fields;
	str = str.substr(0, str.find_last_not_of(on) + 1);
	size_t last = 0, place = 0;
	while((last != string::npos) &&
			(place = str.find_first_of(on, last)) != string::npos) {
		fields.push_back(str.substr(last, place - last));
		last = str.find_first_not_of(on, place);
	}
	if(!str.empty())
		fields.push_back(str.substr(last));
	return fields;
}

bool util::readable(std::string path) {
	return (access(path.c_str(), R_OK) == 0);
}
bool util::executable(std::string path) {
	return (access(path.c_str(), X_OK) == 0);
}

bool util::contains(string &str, string key) {
	return (str.find(key) != string::npos);
}

string util::formatTime(string format) {
	auto n = high_resolution_clock::now();
	return formatTime(n, format);
	//return formatTime(high_resolution_clock::now(), format);
}

string util::formatTime(time_point<high_resolution_clock> &tp, string format) {
	std::locale::global(std::locale("en_US.utf8"));
	time_t tt = system_clock::to_time_t(tp);
	return formatTime(tt, format);
}

string util::formatTime(time_t tt, string format) {
	char tbuf[1024];
	if(std::strftime(tbuf, 1024, format.c_str(), std::gmtime(&tt))) {
		return string(tbuf);
	}
	return "[strftime error]";
}

