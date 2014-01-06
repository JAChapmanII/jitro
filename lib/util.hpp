#ifndef UTIL_HPP
#define UTIL_HPP

#include <string>
#include <vector>
#include <map>
#include <chrono>

namespace util {
	// default time format
	static const std::string defaultTimeFormat = "%D %T";

	std::string trim(std::string str, std::string of = " \t\r\n");
	std::vector<std::string> split(std::string str, std::string on = ", ");

	bool startsWith(std::string str, std::string beg);
	bool endsWith(std::string str, std::string end);

	template<typename T> std::string toString(T val);
	template<typename T> T fromString(std::string val);

	bool readable(std::string path);
	bool executable(std::string path);

	bool contains(std::string &str, std::string key);
	template<typename T> bool contains(std::vector<T> &vector, T key);
	template<typename K, typename V> bool contains(std::map<K, V> &map, K key);

	// time related utility functions
	std::string formatTime(std::string format = defaultTimeFormat);
	std::string formatTime(
			std::chrono::time_point<std::chrono::high_resolution_clock> &tp,
			std::string format = defaultTimeFormat);
	std::string formatTime(std::time_t tt,
			std::string format = defaultTimeFormat);
}

#include "util.imp"

#endif // UTIL_HPP
