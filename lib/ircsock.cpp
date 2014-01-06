#include "ircsock.hpp"
using std::string;
using std::to_string;
using std::vector;

#include <algorithm>
using std::find;
#include <iostream>
using std::cerr;
using std::endl;
#include <fstream>
using std::ofstream;
using std::ios_base;
#include <mutex>
using std::recursive_mutex;
using std::lock_guard;

#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <cstring>

#include "util.hpp"
using util::contains;
using util::startsWith;
using util::toString;
using util::formatTime;
using util::trim;

static string logName = "ircsock.log";
static ofstream logFile;

static void log(string host, string line);
void log(string host, string line) {
	static recursive_mutex mut;
	lock_guard<recursive_mutex> lock(mut);
	// TODO: scope_guard and move into log or something
	static bool _triedInit = false;
	if(!_triedInit) {
		logFile.open(logName, ios_base::app);
		_triedInit = true;
		string dashes = " ------------------------------ ";
		log(host, dashes + " STARTED " + dashes);
	}
	if(logFile.good()) {
		if(trim(line).length() > 0)
			logFile << formatTime("%s") << ":" << host << ":" << line << endl;
	}
}

AddressInfo::AddressInfo(struct addrinfo *ai) : _ai(ai) { }
AddressInfo::AddressInfo(AddressInfo &&rhs) : _ai(rhs._ai) { rhs._ai = nullptr; }
AddressInfo::~AddressInfo() { if(_ai) freeaddrinfo(_ai); }
struct addrinfo *AddressInfo::operator()() { return _ai; }


IRCSock::IRCSock(string host, int port, string nick, string password)
		: _host(host), _port(port), _nick(nick), _password(password) {
}

// TODO: handle disconnecting
IRCSock::~IRCSock() {
}

int IRCSock::connect() {
	// attempt to create socket
	_socket = socket(AF_INET, SOCK_STREAM, 0);
	if(_socket == -1) {
		perror("IRCSock::connect: failed to create socket");
		return 1;
	}

	// lookup address info for host
	AddressInfo ai = lookupDomain();
	if(!ai())
		return 2;

	// connect to host
	int error = ::connect(_socket, ai()->ai_addr, ai()->ai_addrlen);
	if(error == -1) {
		perror("IRCSock::connect");
		return 3;
	}

	// setup our buffered reader object
	_br.setup(_socket, "\r\n");

	send("NICK " + _nick);
	usleep(10000);

	send("USER " + _nick + " jac2.net jac2.net :" + _nick);
	usleep(10000);

	// loop until we receive and error or the connected-go-ahead
	int done = 0;
	while(!done) {
		string str = read();
		// if we didn't recieve a string, wait a bit
		if(str.empty()) {
			usleep(1000);
			continue;
		}

		// if we see nick in use, abort
		if(contains(str, " 433 ")) {
			// TODO: switch to alternate nicks
			cerr << "IRCSock::connect: nick in use!" << endl;
			return 433;
		}

		// if we recieve the nick invalid message, abort
		if(contains(str, " 432 ")) {
			// TODO: same as above
			cerr << "IRCSock::connect: nick contains illegal charaters" << endl;
			return 432;
		}

		// if we see the end of motd code, we're in
		if(contains(str, " 376 ")) {
			if(!_password.empty()) {
				send("PRIVMSG NickServ :identify " + _password);
				usleep(10000);
			}
			return 0;
		}

		// respond to PINGs
		if(startsWith(str, "PING"))
			pong(str);
	}

	// error out (we shouldn't ever get here)
	cerr << "IRCSock::connect: past connect done loop" << endl;
	return -1;
}

bool IRCSock::canRead() {
	return _br.canRead();
}
string IRCSock::read() {
	string l = _br.read();
	log(_host, l);
	return l;
}

ssize_t IRCSock::send(string str) {
	if(!str.empty())
		_wbuf += str + "\r\n";
	if(_wbuf.empty())
		return 0;

	ssize_t wamount = write(_socket, _wbuf.c_str(), _wbuf.length());
	if((wamount < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK)))
		return 0;

	if(wamount < 0) {
		perror("IRCSock::send");
		return wamount;
	} else if(wamount > 0) {
		_wbuf = _wbuf.substr(wamount);
	}

	return wamount;
}

ssize_t IRCSock::pmsg(string target, string msg) {
	return send("PRIVMSG " + target + " :" + msg);
}

int IRCSock::join(string chan) {
	if(contains(_channels, chan))
		return 0;

	send("JOIN " + chan);
	usleep(100000);

	int done = 0;
	while(!done) {
		string str = read();
		// if we didn't recieve a string, wait a bit
		if(str.empty()) {
			usleep(1000);
			continue;
		}

		if(contains(str, " 332 ") || contains(str, " JOIN ")) {
			_channels.push_back(chan);
			return 0;
		}

		// respond to PINGs
		if(startsWith(str, "PING"))
			pong(str);
	}

	return -1;
}
void IRCSock::part(string chan) {
	if(!contains(_channels, chan))
		return;
	send("PART " + chan);
	_channels.erase(find(_channels.begin(), _channels.end(), chan));
}

vector<string> IRCSock::channels() const {
	return _channels;
}

int IRCSock::quit() {
	send("QUIT");
	_channels.clear();
	_br.clear();
	return 0;
}

void IRCSock::pong(string ping) {
	send("PONG" + ping.substr(4));
}

AddressInfo IRCSock::lookupDomain() {
	// if we don't yet have a socket, there is obviously a problem
	if(_socket == -1) {
		cerr << "IRCSock::lookupDomain: socket is nonexistant" << endl;
		return { nullptr };
	}

	// get the string version of our port number
	string sport = to_string(_port);

	// try to get the address info, hinting taht we want IPv4 only
	struct addrinfo *result, hints;
	::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;
	hints.ai_flags = (AI_V4MAPPED | AI_ADDRCONFIG); // defaults for no hints
	int error = ::getaddrinfo(_host.c_str(), sport.c_str(), &hints, &result);

	// if we failed, report the error and abort
	if(error) {
		cerr << "IRCSock::lookupDomain: failed lookup domain: "
			<< gai_strerror(error) << endl;
		return { nullptr };
	}

	// TODO: need {} wrap?
	return result;
}

int IRCSock::reset() {
	cerr << "IRCSock::reset: (host, port, domain, socket, nick): " << _host
		<< ", " << _port << ", " << _domain << ", " << _socket << ", " << _nick
		<< endl;
	close(_socket);
	// TODO: disconnect
	return connect();
}

