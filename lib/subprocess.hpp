#ifndef SUBPROCESS_HPP
#define SUBPROCESS_HPP

#include <string>
#include <vector>
#include <sys/types.h>
#include "bufreader.hpp"

enum class SubprocessStatus { BeforeExec, Exec, AfterExec, INVALID };
std::string toString(SubprocessStatus sstatus);

struct Pipe {
	Pipe() = default;
	Pipe(int fd);
	~Pipe();

	int operator()();
	int steal();
	void close();

	static int make(Pipe *ends);

	private:
		int _fd{-1};
};

struct Subprocess {
	// Create a Subprocess object for a binary
	Subprocess(std::string binary, std::vector<std::string> args = { });
	// Free memory associated with a subproc
	~Subprocess();

	// Actually execute the configured binary
	int run();
	// Attempts to update the status and returns the new one
	SubprocessStatus status();
	// Attempts to return the status code of an AfterExec process
	int statusCode() const;
	// Send the SIGKILL signal to the running subprocess
	int kill();

	// Write a string into the stdin of the subprocess
	ssize_t write(std::string str = "");
	// Returns a valid line read from cout, or blank if nothing was available
	std::string read();

	BufReader &br();

	void flush();

	// get binary name
	std::string binary() const;

	protected:
		std::string _binary{};
		std::vector<std::string> _args{};

		int _pipe[2]{};
		std::string _wbuf{};
		SubprocessStatus _status{SubprocessStatus::BeforeExec};
		pid_t _pid{};
		int _value{};

		BufReader _br{};
};

#endif // SUBPROCESS_HPP
