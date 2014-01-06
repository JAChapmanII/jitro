#include "bufreader.hpp"
using std::string;

#include <iostream>
using std::cerr;
using std::endl;

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "util.hpp"
using util::endsWith;
using util::contains;

static const unsigned readSize = (1024 * 16);

void BufReader::setup(int nFD, string nSplit) {
	_fd = nFD;
	_split = nSplit;
	_eof = false;
	setBlocking(false);
}

bool BufReader::canRead() {
	if(contains(_buf, _split))
		return true;
	tryRead();
	if(contains(_buf, _split))
		return true;
	return false;
}
string BufReader::read() {
	if(_split.empty()) {
		cerr << "BufReader::read: split empty" << endl;
		return "";
	}
	if(!canRead()) {
		if(_eof)
			cerr << "BufReader::read: eof: \"" << _buf << "\"" << endl;
		return "";
	}

	// canRead ensures we have a _split
	size_t loc = _buf.find(_split);
	string msg = _buf.substr(0, loc);
	_buf = _buf.substr(loc + _split.length());
	return msg;
}

void BufReader::tryRead() {
	if(_eof)
		return;

	char tbuf[readSize] = { 0 };
	ssize_t ramount = ::read(_fd, tbuf, readSize);
	if((ramount < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK)))
		return;
	if(ramount < 0) {
		perror("BufReader::tryRead");
		return;
	}

	string ttbuf(tbuf);
	if(ttbuf.length() >= (size_t)ramount)
		ttbuf = ttbuf.substr(0, ramount);
	_buf += ttbuf;

	if(ramount == 0) {
		if(!endsWith(_buf, _split)) {
			cerr << "BufReader::read: EOF reached, end(buf) != split" << endl;
			_buf += _split;
		}
		_eof = true;
	}
}

int BufReader::setBlocking(bool blocking) {
	if(blocking) {
		int ss = fcntl(_fd, F_GETFL, 0);
		return fcntl(_fd, F_SETFL, ss & ~O_NONBLOCK);
	} else {
		int ss = fcntl(_fd, F_GETFL, 0);
		return fcntl(_fd, F_SETFL, ss | O_NONBLOCK);
	}
}

bool BufReader::eof() const {
	return _eof;
}

void BufReader::clear() {
	_fd = -1;
	_split = "";
	_buf.clear();
	_eof = false;
}

// TODO: used?
void BufReader::prefix(string str) {
	_buf = str + _buf;
}
void BufReader::suffix(string str) {
	_buf += str;
}

