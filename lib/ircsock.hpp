#ifndef IRCSOCK_HPP
#define IRCSOCK_HPP

#include <string>
#include <vector>
#include <map>
#include <sys/types.h>
#include "bufreader.hpp"

// simple RAII wrapper around struct addrinfo *
struct AddressInfo {
	AddressInfo(struct addrinfo *ai);
	AddressInfo(AddressInfo &&rhs);
	~AddressInfo();

	AddressInfo(const AddressInfo &rhs) = delete;
	AddressInfo &operator=(const AddressInfo &rhs) = delete;

	struct addrinfo *operator()();

	private:
		struct addrinfo *_ai{nullptr};
};


struct IRCSock {
	enum class Status { Connected, Disconnected, Failed, INVALID };
	enum class NickStatus { NeedsSent, Sent, NoAuth, Verified, Failed, INVALID };
	enum class ChannelStatus { None, Joining, Joined, Parted, Failed, INVALID };
	struct ChannelState {
		ChannelStatus _status{ChannelStatus::None};
		time_t _lastJoin{0};
	};
	enum class CommandType { Nick, User, Identify, Join, Part, Quit, Msg, INVALID };
	struct Command {
		CommandType _type{CommandType::INVALID};
		std::vector<std::string> _args{};

		Command(CommandType it, std::string arg) : _type(it) {
			_args.push_back(arg);
		}
		Command(CommandType it, std::string arg1, std::string arg2) : _type(it) {
			_args.push_back(arg1);
			_args.push_back(arg2);
		}
	};


	IRCSock(std::string host, int port, std::string nick, std::string password);
	~IRCSock();


	// call this every so often to process events and commands
	bool process();


	// interact with the connection through these methods
	void send(std::string str);
	void pmsg(std::string target, std::string msg);
	void join(std::string chan);
	void part(std::string chan);
	void quit();

	std::vector<std::string> read();

	protected:
		AddressInfo lookupDomain();

		int connect();
		void _quit();

		bool _canRead();
		std::string _read();

		ssize_t _trySend();

	protected:
		std::string _host{};
		int _port{};
		int _domain{};
		int _socket{-1};

		bool _hasMOTD{false};

		int _connectionTries{0};
		int _maxConnectionTries{16};
		int _maxConnectionDelay{600};
		time_t _lastConnectionTry{0};
		time_t _lastMessage{0};
		int _pingTimeout{300};

		std::vector<Command> _commandQueue{};
		std::vector<std::string> _channels{};

		Status _mstatus{Status::Disconnected};
		NickStatus _nstatus{NickStatus::NeedsSent};
		std::map<std::string, ChannelStatus> _cstatus{};

		std::string _nick{};
		std::string _password{};

		BufReader _br{};
		std::string _wbuf{};

		std::vector<std::string> _out{};
};

#endif // IRCSOCK_HPP
