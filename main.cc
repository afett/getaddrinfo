#include <netdb.h>
#include <errno.h>
#include <cstring>
#include <string>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/*
   It should be really simple, use getaddrinfo(),set PF_UNSPEC and
   AI_PASSIVE, you get an ipv6 address first, bind to it an receive ipv4
   and ipv6 on the same socket.

   But It's not. The only really reliable way ist to bind two sockets,
   one for v4 and one for v6 both exclusively.

   Major glibc fuckup.

   see:
   https://w3.hepix.org/ipv6-bis/doku.php?id=ipv6:siteconfig

   for a /etc/gai.conf that fixes stupid defaults
*/

namespace netdb {

class addrinfo {
public:
	addrinfo()
	:
		error_(),
		result_(0)
	{
		memset(&hints_, 0, sizeof(hints_));
	}

	bool resolve(const char *host, const char *service)
	{
		reset();
		int ret(getaddrinfo(host, service, &hints_, &result_));
		if (ret == 0) {
			return true;
		}

		if (ret == EAI_SYSTEM) {
			error_ = strerror(errno);
		} else {
			error_ = gai_strerror(ret);
		}
		return false;
	}

	bool foreach(boost::function<bool(::addrinfo const *)> const& cb)
	{
		for (::addrinfo const* ai(result_); ai != 0; ai = ai->ai_next) {
			if (cb(ai)) {
				return true;
			}
		}

		return false;
	}

	::addrinfo & hints()
	{
		return hints_;
	}

	~addrinfo()
	{
		reset();
	}

	std::string error() const
	{
		return error_;
	}

private:
	void reset()
	{
		error_.clear();

		if (result_) {
			::freeaddrinfo(result_);
			result_ = 0;
		}
	}

	std::string error_;
	::addrinfo hints_;
	::addrinfo *result_;
};

}


class Listener {
public:
	Listener()
	:
		fd_(-1)
	{ }

	~Listener()
	{
		if (fd_ != -1) {
			::close(fd_);
		}
	}

	bool listen(const char *host, const char *port)
	{
		netdb::addrinfo resolver;
		resolver.hints().ai_family = PF_UNSPEC;
		resolver.hints().ai_flags = AI_PASSIVE;
		resolver.hints().ai_socktype = SOCK_STREAM;
		if (!resolver.resolve(host, port)) {
			std::cerr << "failed to resolve:" << resolver.error() << "\n";
			return false;
		}

		if (!resolver.foreach(boost::bind(&Listener::try_bind, this, _1))) {
			std::cerr << "failed to bind\n";
			return false;
		}

		return true;
	}

	bool accept()
	{
		int fd(::accept(fd_, 0, 0));
		if (fd == -1) {
			std::cerr << "accept(): " << strerror(errno) << "\n";
			return false;
		}

		close(fd);
		return true;
	}
private:
	bool try_bind(::addrinfo const* ai)
	{
		int fd(::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
		if (fd == -1) {
			return false;
		}

		const int on(1);
		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
			close(fd);
			return false;
		}

		if (::bind(fd, ai->ai_addr, ai->ai_addrlen) == -1) {
			close(fd);
			return false;
		}

		if (::listen(fd, 0) == -1) {
			close(fd);
			return false;
		}

		fd_ = fd;
		return true;
	}

	int fd_;
};

int main()
{
	Listener listener;
	if (!listener.listen(NULL, "1234")) {
		return 1;
	}

	if (!listener.accept()) {
		return 1;
	}

	std::cerr << "success\n";
	return 0;
}
