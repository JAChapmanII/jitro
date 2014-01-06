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
#include <mutex>
using std::mutex;
using std::lock_guard;
#include <thread>
using std::thread;
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

bool done = false;
static string configFile = "jitro.conf";
Config conf;

vector<Subprocess *> sprocs;

struct NetworkManager {
	NetworkManager(string inetwork);
	NetworkManager(NetworkManager &&rhs);
	~NetworkManager();

	NetworkManager(const NetworkManager &rhs) = delete;
	NetworkManager &operator=(const NetworkManager &rhs) = delete;

	void clear();
	void send(string msg);

	IRCSock *isock{nullptr};
	time_t lastMessage{time(NULL)};
	string network{};
	thread mthread{};

	private:
		void manage();

	private:
		mutex _toSendMutex{};
		vector<string> _toSend{};
};

NetworkManager::~NetworkManager() {
	clear();
}

NetworkManager::NetworkManager(string inetwork) {
	network = inetwork;

	string netscope = "irc." + network + ".", server = conf[netscope + "server"];
	if(server.empty()) {
		cerr << "jitro: " + network + " has no defined server" << endl;
		throw 0;
	}

	vector<string> nicks = split(conf[netscope + "nicks"]);
	if(nicks.empty()) {
		cerr << "jitro: " + network + " has no defined nicks" << endl;
		throw 0;
	}

	map<string, string> passwords;
	for(auto nick : nicks)
		passwords[nick] = conf[netscope + nick + ".password"];

	vector<string> channels = split(conf[netscope + "channels"]);
	if(channels.empty()) {
		cerr << "jitro: " + network + " has no defined channels" << endl;
		throw 0;
	}

	cout << "jitro: connecting to " << network
		<< " (" << server << ")" << " as " << nicks[0] << " "
		<< (passwords[nicks[0]].empty() ? "" : "(has password)") << endl;

	isock = new IRCSock(server, 6667, nicks[0], passwords[nicks[0]]);
	if(isock->connect() != 0) {
		cerr << "jitro: unable to connect to server for " << network << endl;
		clear();
		throw 0;
	}

	for(auto channel : channels) {
		cout << "jitro: connecting to " << channel
			<< " on " << network << endl;
		if(isock->join(channel) != 0) {
			cerr << "jitro: unable to connect to channel: " << channel << endl;
		}
	}

	mthread = move(thread(&NetworkManager::manage, this));
}
NetworkManager::NetworkManager(NetworkManager &&rhs)
		: isock(rhs.isock), lastMessage(rhs.lastMessage), network(rhs.network),
		mthread(move(rhs.mthread)), _toSendMutex(), _toSend(move(rhs._toSend)) {
}

void NetworkManager::clear() {
	if(isock) {
		// TODO: we don't have to part everything before QUIT'ing
		for(auto channel : isock->channels()) {
			cout << "jitro: parting from " << channel << " on " << network << endl;
			isock->part(channel);
		}

		isock->quit();

		delete isock;
		isock = nullptr;
	}
	lastMessage = 0;
}
void NetworkManager::send(string msg) {
	lock_guard<mutex> lock(_toSendMutex);
	_toSend.push_back(msg);
}

void alertAllSproc(string network, string msg);
void handleNetwork(string network);

void alertAllSproc(string network, string msg) {
	static mutex alert_mutex;
	lock_guard<mutex> lock(alert_mutex);

	for(auto sproc : sprocs) {
		sproc->write(network + " " + msg);
		sproc->flush();
	}
}

void NetworkManager::manage() {
	while(!done) {
		bool didSomething = 0;
		if(time(NULL) + 1 - lastMessage > 300) {
			cerr << "network, time, lastPing: " << network << ", " << time(NULL)
				<< ", " << lastMessage << endl;
			if(isock->reset() != 0) {
				cerr << "jitro: unable to connect to server for " << network << endl;
				cerr << "jitro: waiting and trying to reconnect" << endl;
				sleep(10);
				// continue;
			} else {
				lastMessage = time(NULL);
			}
		}

		{ // dispatch all waiting messages
			lock_guard<mutex> lock(_toSendMutex);
			for(auto msg : _toSend) {
				cerr << "jitro: sent \"" << msg << "\" to " << network << endl;
				isock->send(msg);
			}
			_toSend.clear();
		}

		// copy from IRC socket to subprocess stdin
		for(string line = isock->read(); !line.empty(); line = isock->read()) {
			didSomething = true;
			lastMessage = time(NULL); // any server message counts
			if(startsWith(line, "PING"))
				isock->send("PONG" + line.substr(4));
			else
				// pipe to all subprocesses
				alertAllSproc(network, line); // TODO: repeatedly acquires lock...
		}

		if(!didSomething)
			usleep(1000);
	}
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

	vector<NetworkManager *> netmans;
	for(auto network : networks) {
		try {
			netmans.push_back(new NetworkManager{ network });
		} catch(int) {
			// woohoo empty catch
		}
	}

	for(auto binary : binaries)
		sprocs.push_back(new Subprocess(binary));

	// keep main thread alive
	while(!done) {
		bool didSomething = false;
		for(auto sproc : sprocs) {
			if(sproc->status() == SubprocessStatus::AfterExec) {
				cout << "jitro: subproccess \"" << sproc->binary()
					<< "\" returned: " << sproc->statusCode() << endl;
				sproc->kill();
			}
			if(sproc->status() != SubprocessStatus::Exec) {
				cout << "jitro: creating subprocess \"" << sproc->binary()
					<< "\"" << endl;
				if(sproc->run() != 0) {
					cerr << "jitro: unable to run subprocess!?" << endl;
					usleep(10000);
					continue;
				}
				didSomething = true;
				usleep(10000);
			}

			// copy from subprocesses stdout to the IRC socket
			for(string line = sproc->read(); !line.empty(); line = sproc->read()) {
				cerr << "jitro: read \"" << line << "\" from " << sproc->binary() << endl;
				string destination = line.substr(0, line.find(" ")),
						msg = line.substr(line.find(" ") + 1);
				for(auto netman : netmans)
					if(destination == "broadcast" || netman->network == destination)
						netman->send(msg);
			}

			// if the subprocess has closed it's stdout, close it down
			if(sproc->br().eof()) {
				cout << "jitro: subproc \"" << sproc->binary()
					<< "\" has returned EOF" << endl;
				sproc->kill();
			}
		}

		if(!didSomething)
			usleep(1000);
	}

	return 0;
}

