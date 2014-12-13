#include "ircsock.hpp"
using std::string;
using std::to_string;
using std::vector;

#include <algorithm>
using std::find;
using std::min;
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
using util::split;

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
	_quit();
}

string extractNick(string from);
string extractNick(string from) {
	if(from.find("!") == string::npos)
		return from;
	return from.substr(0, from.find("!"));
}

void IRCSock::_quit() {
	if(_socket < 0 || _mstatus == Status::Disconnected)
		return;
	send("QUIT :goodbye"); // TODO
	_trySend();

	_connectionTries = 0;
	_lastConnectionTry = 0;

	_mstatus = Status::Disconnected;
	_nstatus = NickStatus::NeedsSent;
	_cstatus.clear();

	_hasMOTD = false;

	_br.clear();

	usleep(1000);
	if(_socket >= 0)
		close(_socket);
}

bool IRCSock::process() {
	switch(_mstatus) {
		case Status::Connected:
			break;
		case Status::Disconnected: {
			time_t now = time(NULL);
			if(_connectionTries > _maxConnectionTries) {
				_mstatus = Status::Failed;
				return false;
			}
			// make sure we've waited long enough before retrying
			int delay = min(1 << _connectionTries, _maxConnectionDelay);
			if((now - _lastConnectionTry) < delay)
				return false;

			// attempt to connect
			this->connect();
			return true;
		}
		case Status::Failed:
		case Status::INVALID:
		default:
			return false;
	}

	time_t now = time(NULL);
	// if we've pinged out
	if(now - _lastMessage > _pingTimeout) {
		_quit();
		return true;
	}

	bool didSomething = !_commandQueue.empty();
	vector<Command> ncomms{};
	for(int i = 0; i < _commandQueue.size(); ++i) {
		Command &comm = _commandQueue[i];

		switch(comm._type) {
			case CommandType::Nick:
				send("NICK " + comm._args[0]);
				//usleep(10000);
				_nstatus = NickStatus::Sent;
				break;
			case CommandType::User:
				send("USER " + comm._args[0] + " 0 * :" + comm._args[0]);
				//usleep(10000);
				break;
			case CommandType::Identify:
				if(!_password.empty()) {
					send("PRIVMSG NickServ :identify " + comm._args[0]);
					//usleep(10000);
					_nstatus = NickStatus::Verified;
				} else  {
					_nstatus = NickStatus::NoAuth;
				}
				break;
			case CommandType::Join:
				if(_hasMOTD) {
					send("JOIN " + comm._args[0]);
					//usleep(100000);
				} else
					ncomms.push_back(comm);
				break;
			case CommandType::Part:
				send("PART " + comm._args[0]);
				_cstatus[comm._args[0]] = ChannelStatus::Parted;
				break;
			case CommandType::Msg:
				// TODO: join chan if not in chan?
				send("PRIVMSG " + comm._args[0] + " :" + comm._args[1]);
				break;
			case CommandType::Quit:
				_quit();
				return true;
			case CommandType::INVALID:
			default:
				break; // TODO
		}
	}
	_commandQueue = ncomms;

	// try sending anything we may be waiting to send
	didSomething |= _trySend() > 0;

	didSomething |= _canRead();
	while(_canRead()) {
		string line = _read();
		if(line.empty())
			continue;

		vector<string> fields = split(line);
		if(fields.size() < 2)
			continue;
		string from = fields[0], command = fields[1];

		// if we see nick in use, abort
		if(command == "433") {
			_nstatus = NickStatus::Failed;
			// TODO: switch to alternate nicks
			cerr << "IRCSock::connect: nick in use!" << endl;
			throw 433;
		}

		// if we recieve the nick invalid message, abort
		if(command == "432") {
			_nstatus = NickStatus::Failed;
			// TODO: same as above
			cerr << "IRCSock::connect: nick contains illegal charaters" << endl;
			throw 432;
		}

		// if we see the end of motd code, we're in and may need to auth
		if(command == "376") {
			_commandQueue.push_back(Command(CommandType::Identify, _password));
			_hasMOTD = true;
		}

		// TODO: names
		// if(contains(line, " 332 ") || 

		// somebody joined a channel
		if(command == "JOIN") {
			// we joined a channel
			if(extractNick(from) == _nick) {
				_cstatus[fields[2]] = ChannelStatus::Joined;
			}
		}

		// respond to PINGs
		if(startsWith(line, "PING"))
			send("PONG" + line.substr(4));
	}

	// try sending anything we may be waiting to send
	didSomething |= _trySend() > 0;

	return didSomething;
}

int IRCSock::connect() {
	_connectionTries++;
	_lastConnectionTry = time(NULL);
	cerr << "IRCSock::connect: attempting to connect to " << _host << endl;

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

	_mstatus = Status::Connected;
	_commandQueue.push_back(Command(CommandType::Nick, _nick));
	_commandQueue.push_back(Command(CommandType::User, _nick));
	for(auto &chan : _channels)
		_commandQueue.push_back(Command(CommandType::Join, chan));

	time_t now = time(NULL);
	_lastMessage = now;

	return 0;
}

bool IRCSock::_canRead() {
	return _br.canRead();
}
string IRCSock::_read() {
	string l = _br.read();
	if(!l.empty()) {
		_lastMessage = time(NULL);
		_out.push_back(l);
	}
	log(_host, l);
	return l;
}

ssize_t IRCSock::_trySend() {
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


void IRCSock::send(string str) {
	if(!str.empty())
		_wbuf += str + "\r\n";
}
void IRCSock::pmsg(string target, string msg) {
	_commandQueue.push_back(Command(CommandType::Msg, target, msg));
}

void IRCSock::join(string chan) {
	if(_cstatus.find(chan) == _cstatus.end()
			|| _cstatus[chan] == ChannelStatus::Parted
			|| _cstatus[chan] == ChannelStatus::Failed) {
		_commandQueue.push_back(Command(CommandType::Join, chan));
		_channels.push_back(chan);
	}
}
void IRCSock::part(string chan) {
	if(_cstatus.find(chan) == _cstatus.end()
			|| _cstatus[chan] != ChannelStatus::Joined)
		return;
	_commandQueue.push_back(Command(CommandType::Part, chan));

	auto it = find(_channels.begin(), _channels.end(), chan);
	if(it != _channels.end())
		_channels.erase(it);
}

void IRCSock::quit() {
	_commandQueue.push_back(Command(CommandType::Quit, "goodbype"));
}

vector<string> IRCSock::read() {
	vector<string> out = _out;
	_out.clear();
	return out;
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

