#include <netdb.h>
#include <errno.h>
#include <cstring>
#include <string>
#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <functional>

/*
   It should be really simple, use getaddrinfo(), set PF_UNSPEC and
   AI_PASSIVE, you get an ipv6 address first, bind to it and receive ipv4
   and ipv6 on the same socket.

   But It's not. The only really reliable way ist to bind two sockets,
   one for v4 and one for v6 both exclusively.

   Major glibc fuckup.

   see:
   http://hepix-ipv6.web.cern.ch/content/slc6-actually-glibc-29-change-aipassive-bind-pfunspec-getaddrinfo-preference

   for a /etc/gai.conf that fixes stupid defaults

   see also:
   https://sourceware.org/bugzilla/show_bug.cgi?id=9981
*/

namespace netdb {

class addrinfo {
public:
	addrinfo()
	:
		error_(),
		result_(nullptr)
	{
		::memset(&hints_, 0, sizeof(hints_));
	}

	bool resolve(const char *host, const char *service)
	{
		reset();
		auto ret(::getaddrinfo(host, service, &hints_, &result_));
		if (ret == 0) {
			return true;
		}

		error_ = (ret == EAI_SYSTEM) ?
			::strerror(errno) : ::gai_strerror(ret);
		return false;
	}

	bool foreach(std::function<bool(::addrinfo const *)> const& cb)
	{
		for (auto ai(result_); ai != nullptr; ai = ai->ai_next) {
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
			result_ = nullptr;
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
		close_fd();
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

		if (!resolver.foreach(std::bind(&Listener::try_bind, this, std::placeholders::_1))) {
			std::cerr << "failed to bind\n";
			return false;
		}

		return true;
	}

	bool accept()
	{
		auto fd(::accept(fd_, 0, 0));
		if (fd == -1) {
			std::cerr << "accept(): " << ::strerror(errno) << "\n";
			return false;
		}

		::close(fd);
		return true;
	}

private:
	bool try_bind(::addrinfo const* ai)
	{
		fd_ = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd_ == -1) {
			return false;
		}

		auto const on(1);
		if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
			close_fd();
			return false;
		}

		if (::bind(fd_, ai->ai_addr, ai->ai_addrlen) == -1) {
			close_fd();
			return false;
		}

		if (::listen(fd_, 0) == -1) {
			close_fd();
			return false;
		}

		return true;
	}

	void close_fd()
	{
		if (fd_ != -1) {
			::close(fd_);
			fd_ = -1;
		}
	}

	int fd_;
};

int main()
{
	Listener listener;
	if (!listener.listen(nullptr, "1234")) {
		return 1;
	}

	if (!listener.accept()) {
		return 1;
	}

	std::cerr << "success\n";
	return 0;
}
