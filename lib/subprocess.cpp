#include "subprocess.hpp"
using std::string;
using std::vector;

#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>

#include "util.hpp"
using util::executable;

#include <iostream>
using std::cerr;
using std::endl;

string toString(SubprocessStatus sstatus) {
	switch(sstatus) {
		case SubprocessStatus::BeforeExec: return "BeforeExec";
		case SubprocessStatus::Exec: return "Exec";
		case SubprocessStatus::AfterExec: return "AfterExec";
		default: case SubprocessStatus::INVALID: return "INVALID";
	}
}

Pipe::Pipe(int fd) : _fd(fd) { }
Pipe::~Pipe() { close(); }
int Pipe::operator()() { return _fd; }
int Pipe::steal() {
	int fd = _fd;
	_fd = 0;
	return fd;
}
void Pipe::close() {
	if(_fd >= 0)
		::close(_fd); // TODO: close errors
	_fd = -1;
}
int Pipe::make(Pipe *ends) {
	int fd_ends[2] = { -1, -1 }, fail = ::pipe(fd_ends);
	if(fail) {
		perror("Pipe::make");
		return fail;
	}
	ends[0]._fd = fd_ends[0];
	ends[1]._fd = fd_ends[1];
	return fail;
}

Subprocess::Subprocess(string ibinary, vector<string> args)
		: _binary(ibinary), _args(args) {
}
Subprocess::~Subprocess() {
	if(_status == SubprocessStatus::Exec)
		kill();
}

int Subprocess::run() {
	// if we're not in the before exec phase, abort
	if(_status != SubprocessStatus::BeforeExec) {
		cerr << "Subprocess::run: not in before exec state: "
			<< toString(_status) << endl;
		return -1;
	}

	// if the binary doesn't exist, abort
	if(!executable(_binary)) {
		_status = SubprocessStatus::INVALID;
		cerr << "Subprocess::run: binary not executable" << endl;
		return -1;
	}

	Pipe left[2], right[2];
	// if we can't create the left pipe, abort
	if(int fail = Pipe::make(left))
		return fail;
	// if we can't create the right pipe, abort
	if(int fail = Pipe::make(right))
		return fail;

	// if we can't fork, abort
	_pid = ::fork();
	if(_pid == -1) {
		cerr << "Subprocess::run: forking failed" << endl;
		return -2;
	}

	// if we're the child, execv the binary
	if(_pid == 0) {
		::dup2(left[0](), 0);
		left[0].close();
		left[1].close();

		::dup2(right[1](), 1);
		right[0].close();
		right[1].close();

		char *bstr = (char *)_binary.c_str();
		char **argv = new char*[_args.size() + 2];
		argv[0] = ::strdup(_binary.c_str());
		for(unsigned i = 0; i < _args.size(); ++i)
			argv[1 + i] = (char *)_args[i].c_str();
		argv[1 + _args.size()] = NULL;

		execv(bstr, argv);
		perror("Subprocess::run: after execv");
		return -99;
	}

	// if we're main, close uneeded ends and copy fds
	left[0].close();
	right[1].close();
	_pipe[0] = right[0].steal();
	_pipe[1] = left[1].steal();
	_br.setup(_pipe[0], "\n");

	_status = SubprocessStatus::Exec;
	return 0;
}

SubprocessStatus Subprocess::status() {
	if(_status != SubprocessStatus::Exec)
		return _status;
	pid_t pid = waitpid(_pid, &_value, WNOHANG);
	if(pid == _pid)
		_status = SubprocessStatus::AfterExec;
	return _status;
}

int Subprocess::statusCode() const {
	if(_status != SubprocessStatus::AfterExec)
		return -1;
	return WEXITSTATUS(_value);
}


int Subprocess::kill() {
	if(_status == SubprocessStatus::AfterExec) {
		_status = SubprocessStatus::BeforeExec;
		return 0;
	}
	if(_status != SubprocessStatus::Exec)
		return -1;
	int ret = ::kill(_pid, SIGKILL);
	_status = SubprocessStatus::BeforeExec;
	return ret;
}

	//return fdopen(subproc->pipe[1], "w");
ssize_t Subprocess::write(string str) {
	if(!str.empty())
		_wbuf += str + "\n";

	if(_wbuf.empty())
		return 0;

	ssize_t wamount = ::write(_pipe[1], _wbuf.c_str(), _wbuf.length());
	if((wamount < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK)))
		return 0;

	if(wamount < 0) {
		perror("Subprocess::write");
		return wamount;
	} else if(wamount > 0) {
		_wbuf = _wbuf.substr(wamount);
	}

	return wamount;
}
string Subprocess::read() {
	return _br.read();
}

BufReader &Subprocess::br() {
	return _br;
}

void Subprocess::flush() {
	while(!_wbuf.empty())
		write();
	::syncfs(_pipe[1]);
}

string Subprocess::binary() const {
	return _binary;
}
