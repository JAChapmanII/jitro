#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <map>
#include <string>

// TODO: write support
struct Config {
	int load(std::string fileName);
	void clear();

	std::string defaultScope() const;
	void defaultScope(std::string scope);

	bool has(std::string scopedVariable);
	bool has(std::string scope, std::string variable);

	std::string get(std::string scopedVariable);
	std::string get(std::string scope, std::string variable);

	void set(std::string scopedVariable, std::string value);
	void set(std::string scope, std::string variable, std::string value);

	std::string &operator[](std::string scopedVariable);

	std::map<std::string, std::string>::iterator begin();
	std::map<std::string, std::string>::iterator end();

	static std::string scopeVariable(std::string scope, std::string variable);

	protected:
		std::string _defaultScope{};
		std::map<std::string, std::string> _map{};
};

#endif // CONFIG_HPP
