#ifndef BUFREADER_HPP
#define BUFREADER_HPP

#include <string>

// BufReader provides buffered read support from a file descriptor.
struct BufReader {
	void setup(int nFD, std::string nSplit);

	bool canRead();
	std::string read();

	// Switch a BufReader into blocking read mode (default is nonblocking)
	int setBlocking(bool blocking);

	bool eof() const;

	void clear();

	void prefix(std::string str);
	void suffix(std::string str);

	protected:
		void tryRead();

	protected:
		int _fd{-1};
		std::string _split{"\r\n"};
		std::string _buf{};
		bool _eof{true};
};

#endif // BUFREADER_HPP
