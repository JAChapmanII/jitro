#ifndef IRCSOCK_HPP
#define IRCSOCK_HPP

#include <string>
#include <vector>
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
	IRCSock(std::string host, int port, std::string nick, std::string password);
	~IRCSock();

	int connect();

	bool canRead();
	std::string read();

	ssize_t send(std::string str);

	ssize_t pmsg(std::string target, std::string msg);

	int join(std::string chan);
	void part(std::string chan);

	std::vector<std::string> channels() const;

	int quit();

	int reset();

	protected:
		void pong(std::string ping);
		AddressInfo lookupDomain();

	protected:
		std::string _host{};
		int _port{};
		int _domain{};
		int _socket{-1};

		std::string _nick{};
		std::string _password{};
		std::vector<std::string> _channels{};

		BufReader _br{};
		std::string _wbuf{};
};

#endif // IRCSOCK_HPP
