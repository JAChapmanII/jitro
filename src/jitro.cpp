#include <iostream>
using std::cout;
using std::cerr;
using std::endl;
#include <string>
using std::string;
#include <vector>
using std::vector;
#include <map>
using std::map;
#include <memory>
using std::move;

#include <unistd.h>
#include <time.h>

#include "ircsock.hpp"
#include "subprocess.hpp"
#include "config.hpp"
#include "util.hpp"
using util::contains;
using util::split;
using util::executable;
using util::startsWith;
using util::fromString;

bool done = false;
static string configFile = "jitro.conf";
Config conf;

vector<string> getChannelsForNetwork(string network);

vector<string> getChannelsForNetwork(string network) {
	string netscope = "irc." + network + ".";

	vector<string> channels = split(conf[netscope + "channels"]);
	if(channels.empty()) {
		cerr << "jitro: " + network + " has no defined channels" << endl;
		throw 0;
	}

	return channels;
}

struct ConnectionManager {
	ConnectionManager(string inetwork);
	~ConnectionManager();

	ConnectionManager(ConnectionManager &&rhs);
	ConnectionManager(const ConnectionManager &rhs) = delete;
	ConnectionManager &operator=(const ConnectionManager &rhs) = delete;

	void manage();

	void write(string line);
	vector<string> read();

	string name();

	protected:
		IRCSock *_isock{nullptr};
		vector<string> _out{};
		vector<string> _in{};
		string _network{};
};

ConnectionManager::~ConnectionManager() {
	if(_isock) {
		_isock->quit();
		_isock->process();
		delete _isock;
	}
}
ConnectionManager::ConnectionManager(ConnectionManager &&rhs) :
		_isock(rhs._isock), _out(rhs._out), _in(rhs._in), _network(rhs._network) {
	rhs._isock = nullptr;
}

ConnectionManager::ConnectionManager(string inetwork) : _network(inetwork) {
	string netscope = "irc." + _network + ".", server = conf[netscope + "server"];
	if(server.empty()) {
		cerr << "jitro: " + _network + " has no defined server" << endl;
		throw 0;
	}

	string sport = conf[netscope + "port"];
	if(sport.empty()) sport = "6667";

	int port = fromString<int>(sport);

	vector<string> nicks = split(conf[netscope + "nicks"]);
	if(nicks.empty()) {
		cerr << "jitro: " + _network + " has no defined nicks" << endl;
		throw 0;
	}

	map<string, string> passwords;
	for(auto nick : nicks)
		passwords[nick] = conf[netscope + nick + ".password"];

	vector<string> channels = split(conf[netscope + "channels"]);
	if(channels.empty()) {
		cerr << "jitro: " + _network + " has no defined channels" << endl;
		throw 0;
	}

	cout << "jitro: connecting to " << _network
		<< " (" << server << ":" << port << ")" << " as " << nicks[0] << " "
		<< (passwords[nicks[0]].empty() ? "" : "(has password)") << endl;

	_isock = new IRCSock(server, port, nicks[0], passwords[nicks[0]]);
	for(auto &chan : channels) {
		cerr << "jitro: joining " << chan << " on " << _network << endl;
		_isock->join(chan);
	}
}

string ConnectionManager::name() {
	return _network;
}
void ConnectionManager::write(string msg) {
	_in.push_back(msg);
}
vector<string> ConnectionManager::read() {
	vector<string> out = _out;
	_out.clear();
	return out;
}

void ConnectionManager::manage() {
	_isock->process();

	// dispatch all waiting messages
	for(auto msg : _in) {
		cerr << "jitro: sent \"" << msg << "\" to " << _network << endl;
		_isock->send(msg);
	}
	_in.clear();

	vector<string> out = _isock->read();
	_out.reserve(_out.size() + out.size());
	for(auto &line : out) {
		// don't pass PINGs out
		if(startsWith(line, "PING"))
			continue;
		_out.push_back(line);
	}
}



struct BinaryManager {
	BinaryManager(string binary);

	BinaryManager(BinaryManager &&rhs);
	BinaryManager(const BinaryManager &rhs) = delete;
	BinaryManager &operator=(const BinaryManager &rhs) = delete;

	void manage();

	void write(string line);
	vector<string> read();

	string name();

	protected:
		Subprocess *_sproc{nullptr};
		bool _failed{false};
		vector<string> _out{};
		vector<string> _in{};
};

BinaryManager::BinaryManager(string binary) : _sproc(new Subprocess(binary)) { }
BinaryManager::BinaryManager(BinaryManager &&rhs) : _sproc(rhs._sproc),
		_failed(rhs._failed), _out(rhs._out), _in(rhs._in) {
	rhs._sproc = nullptr;
}

void BinaryManager::manage() {
	if(_failed)
		return;

	if(_sproc->status() == SubprocessStatus::AfterExec) {
		cout << "jitro: subproccess \"" << _sproc->binary()
			<< "\" returned: " << _sproc->statusCode() << endl;
		_sproc->kill();
	}

	if(_sproc->status() != SubprocessStatus::Exec) {
		cout << "jitro: creating subprocess \"" << _sproc->binary()
			<< "\"" << endl;
		if(_sproc->run() != 0) {
			cerr << "jitro: unable to run subprocess!?" << endl;
			_failed = true;
		}
		return;
	}

	for(auto &l : _in) {
		_sproc->write(l);
		_sproc->flush();
	}
	_in.clear();

	for(string line = _sproc->read(); !line.empty(); line = _sproc->read()) {
		_out.push_back(line);
	}

	// if the subprocess has closed it's stdout, close it down
	if(_sproc->br().eof()) {
		cout << "jitro: subproc \"" << _sproc->binary()
			<< "\" has returned EOF" << endl;
		_sproc->kill();
	}
}

vector<string> BinaryManager::read() {
	vector<string> out = _out;
	_out.clear();
	return out;
}
void BinaryManager::write(string line) {
	_in.push_back(line);
}

string BinaryManager::name() {
	return _sproc->binary();
}


int main(int argc, char **argv) {
	vector<string> args;
	for(unsigned arg = 1; arg < (unsigned)argc; ++arg)
		args.push_back(argv[arg]);

	conf.load(configFile);

	if(contains(args, (string)"--dump-config"))
		for(auto i : conf)
			cout << i.first << " = " << i.second << endl;

	vector<string> allBinaries = split(conf["core.binary"]), binaries;
	for(auto binary : allBinaries) {
		if(!executable(binary)) {
			cerr << "jitro: configured binary not executable: \"" << binary
				<< "\"" << endl;
		} else {
			binaries.push_back(binary);
		}
	}
	if(binaries.empty()) {
		cerr << "jitro: error: no executable binaries found" << endl;
		return 1;
	}

	if(!conf.has("irc.networks")) {
		cerr << "jitro: no IRC networks defined." << endl;
		return 1;
	}

	vector<string> networks = split(conf["irc.networks"]);
	if(networks.empty()) {
		cerr << "jitro: IRC networks does not contain any networks?" << endl;
		return 1;
	}

	vector<BinaryManager> bins;
	for(auto binary : binaries)
		bins.emplace_back(binary);

	vector<ConnectionManager> conns;
	for(auto network : networks)
		conns.emplace_back(network);

	int ms = 1000;

	// keep main thread alive
	while(!done) {
		for(auto &bin : bins) {
			bin.manage();

			// copy from subprocesses stdout to the IRC socket
			vector<string> lines = bin.read();
			for(auto &line : lines) {
				cerr << "jitro: read \"" << line << "\" from " << bin.name() << endl;
				string destination = line.substr(0, line.find(" ")),
					msg = line.substr(line.find(" ") + 1);

				bool broadcast = destination == "broadcast";
				if(startsWith(msg, "QUIT")) {
					cerr << "jitro: read QUIT message" << endl;
					done = true;
				}

				for(auto &conn : conns)
					if(broadcast || conn.name() == destination)
						conn.write(msg);
			}
		}

		for(auto &conn : conns) {
			conn.manage();

			// copy from irc to binaries
			vector<string> lines = conn.read();
			for(auto &line : lines) {
				for(auto &bin : bins)
					bin.write(conn.name() + " " + line);
			}
		}

		usleep(10 * ms);
	}

	return 0;
}

