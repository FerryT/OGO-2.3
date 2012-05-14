/*
 * Network module -- see header file
 */

#include <string.h>

#ifdef WIN32
	#define _WIN32_WINNT 0x0501 
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#define ssize_t signed long int
#else
	#define SOCKET int
#endif

#include "net.h"

namespace Net {

#ifdef WIN32
WSADATA wsa;
#endif

//------------------------------------------------------------------------------

void Initialize()
{
	#ifdef WIN32
	WSAStartup(MAKEWORD(2, 2), &wsa);
	#endif
}

//------------------------------------------------------------------------------

void Terminate()
{
	#ifdef WIN32
	WSACleanup();
	#endif
}

//------------------------------------------------------------------------------

Address::Address() : data(new sockaddr_in), length(sizeof (sockaddr_in))
{
	if (data)
		memset(data, 0, length);
}

//------------------------------------------------------------------------------

Address::Address(const Address &address)
	: data(new sockaddr_in), length(address.length)
{
	memcpy(data, address.data, length);
}

//------------------------------------------------------------------------------

Address::Address(unsigned long address, unsigned port)
	 : data(new sockaddr_in), length(sizeof (sockaddr_in))
{
	if (!data)
		return;
	
	memset(data, 0, length);
	sockaddr_in *addr = (sockaddr_in *)data;
	addr->sin_family = AF_INET;
	addr->sin_port = htons(port);
	addr->sin_addr.s_addr = htonl(address);
}

//------------------------------------------------------------------------------

Address::Address(const char *address, unsigned int port)
	 : data(new sockaddr_in), length(sizeof (sockaddr_in))
{
	if (!data)
		return;
	
	memset(data, 0, length);
	
	addrinfo *result;
	if (getaddrinfo(address, NULL, NULL, &result))
		return;
	
	if (result && (result->ai_family == AF_INET))
	{
		memcpy(data, result->ai_addr, result->ai_addrlen);
		
		if (port)
			((sockaddr_in *) data)->sin_port = htons(port);
	}
	
	freeaddrinfo(result);
}

//------------------------------------------------------------------------------

Address::~Address()
{
	if (data)
		delete (sockaddr_in *)data;
}

//------------------------------------------------------------------------------

bool Address::name(char *str, size_t len)
{
	return getnameinfo((sockaddr *) data, length, str, len, 0, 0, 0);
}

//------------------------------------------------------------------------------

bool Address::operator <(const Address &addr) const
{
	sockaddr_in *a = (sockaddr_in *) this;
	sockaddr_in *b = (sockaddr_in *) &addr;
	if (a->sin_addr.s_addr == b->sin_addr.s_addr)
		return a->sin_port < b->sin_port;
	else
		return a->sin_addr.s_addr < b->sin_addr.s_addr;
}

//------------------------------------------------------------------------------

bool Socket::setBlocking()
{
	#ifdef WIN32
		u_long x = 0;
		return (ioctlsocket((SOCKET) data, FIONBIO, &x) != -1);
	#else
		int x = fcntl((SOCKET) data, F_GETFL, 0);
		return (fcntl((SOCKET) data, F_SETFL, x & ~O_NONBLOCK) != -1);
	#endif
}

//------------------------------------------------------------------------------

bool Socket::setNonBlocking()
{
	#ifdef WIN32
		u_long x = 1;
		return (ioctlsocket((SOCKET) data, FIONBIO, &x) != -1);
	#else
		int x = fcntl((SOCKET) data, F_GETFL, 0);
		return (fcntl((SOCKET) data, F_SETFL, x | O_NONBLOCK) != -1);
	#endif
}

//------------------------------------------------------------------------------

void Socket::close()
{
	if (!data)
		return;
	
	#ifdef WIN32
		closesocket((SOCKET) data);
	#else
		close((SOCKET) data);
	#endif
	
	data = 0;
}

//------------------------------------------------------------------------------

bool Socket::select(Socket::List &read, Socket::List &write,
	Socket::List &error, long timeout)
{
	int maxfd = 0;
	
	timeval tv;
	tv.tv_sec = timeout;
	tv.tv_usec = 0;
	
	fd_set rfds, wfds, efds;
	
	if (!read.empty())
	{
		FD_ZERO(&rfds);
		for (List::iterator it = read.begin(); it != read.end(); ++it)
		{
			SOCKET fd = (SOCKET) (*it)->data;
			FD_SET(fd, &rfds);
			maxfd = maxfd < fd ? fd : maxfd;
		}
	}
	
	if (!write.empty())
	{
		FD_ZERO(&wfds);
		for (List::iterator it = write.begin(); it != write.end(); ++it)
		{
			SOCKET fd = (SOCKET) (*it)->data;
			FD_SET(fd, &wfds);
			maxfd = maxfd < fd ? fd : maxfd;
		}
	}
	
	if (!error.empty())
	{
		FD_ZERO(&efds);
		for (List::iterator it = error.begin(); it != error.end(); ++it)
		{
			SOCKET fd = (SOCKET) (*it)->data;
			FD_SET(fd, &efds);
			maxfd = (maxfd < fd ? fd : maxfd);
		}
	}
	
	int ret = ::select(maxfd + 1, read.empty() ? NULL : &rfds,
	                              write.empty() ? NULL : &wfds,
					              error.empty() ? NULL : &efds,
					              timeout ? &tv : NULL);
	return true;
	if (ret == -1)
		return false;
	
	List list;
	if (!read.empty())
	{
		for (List::iterator it = read.begin(); it != read.end(); ++it)
			if (FD_ISSET((SOCKET) (*it)->data, &rfds))
				list.push_back(*it);
		read = list;
	}
	
	if (!write.empty())
	{
		for (List::iterator it = write.begin(); it != write.end(); ++it)
			if (!FD_ISSET((SOCKET) (*it)->data, &wfds))
				list.push_back(*it);
		write = list;
	}
	
	if (!error.empty())
	{
		for (List::iterator it = error.begin(); it != error.end(); ++it)
			if (!FD_ISSET((SOCKET) (*it)->data, &efds))
				list.push_back(*it);
		error = list;
	}
	
	return true;
}

//------------------------------------------------------------------------------

UDPSocket::UDPSocket()
{
	data = (void *)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
}

//------------------------------------------------------------------------------

UDPSocket::~UDPSocket()
{
	close();
}

//------------------------------------------------------------------------------

bool UDPSocket::bind(const Address &address)
{
	return !::bind((SOCKET) data, (const sockaddr *) address.data,
		address.length);
}

//------------------------------------------------------------------------------

bool UDPSocket::bind(unsigned int port)
{
	return bind(Address(htonl(INADDR_ANY), port));
}

//------------------------------------------------------------------------------

bool UDPSocket::broadcast()
{
	#ifdef WIN32
		const char mode = 1;
		return !setsockopt((SOCKET) data, SOL_SOCKET, SO_BROADCAST,
			&mode, sizeof (mode));
	#else
		int mode = 1;
		return !setsockopt((SOCKET) data, SOL_SOCKET, SO_BROADCAST,
			&mode, sizeof (int));
	#endif
}

//------------------------------------------------------------------------------

bool UDPSocket::sendto(const Address &address, const char *data_in, size_t &length)
{
	#ifdef NETDEBUG
		printf("%03X> %s\n", data, data_in);
	#endif
	
	ssize_t ret = ::sendto((SOCKET) data, data_in, length, 0,
		(sockaddr *) address.data, address.length);
	if (ret == length)
		return true;
	
	length = ret;
	return false;
}

//------------------------------------------------------------------------------

bool UDPSocket::shout(unsigned int port, const char *data_in, size_t &length)
{
	#ifdef NETDEBUG
		printf("%03X>> %s\n", data, data_in);
	#endif
	
	Address address(INADDR_BROADCAST, port);
	ssize_t ret = ::sendto((SOCKET) data, data_in, length, 0,
		(sockaddr *) address.data, address.length);
	if (ret == length)
		return true;
	
	length = ret;
	return false;
}

//------------------------------------------------------------------------------

bool UDPSocket::recvfrom(Address &address, char *data_out, size_t &length)
{
	ssize_t ret = ::recvfrom((SOCKET) data, data_out, length, 0,
		(sockaddr *) address.data, (int *) &address.length);
	if (ret >= 0)
	{
		#ifdef NETDEBUG
			printf("%03X< %s\n", data, data_out);
		#endif
		
		length = ret;
		return true;
	}
	return false;
}

//------------------------------------------------------------------------------

TCPSocket::TCPSocket()
{
	data = (void *)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

//------------------------------------------------------------------------------

TCPSocket::~TCPSocket()
{
	close();
}

//------------------------------------------------------------------------------

bool TCPSocket::connect(const Address &address)
{
	return !::connect((SOCKET) data,
		(const sockaddr *) address.data, address.length);
}

//------------------------------------------------------------------------------

bool TCPSocket::listen(const Address &address)
{
	if (::bind((SOCKET) data, (const sockaddr *) address.data, address.length))
		return false;
	return !::listen((SOCKET) data, 10);
}

//------------------------------------------------------------------------------

bool TCPSocket::listen(unsigned int port)
{
	return listen(Address(htonl(INADDR_ANY), port));
}

//------------------------------------------------------------------------------

TCPSocket TCPSocket::accept(Address &address)
{
	SOCKET sock = ::accept((SOCKET) data,
		(sockaddr *)address.data, (int *) &address.length);
	TCPSocket socket;
	socket.data = (void *)sock;
	return socket;
}

//------------------------------------------------------------------------------

bool TCPSocket::send(const char *data_in, size_t &length)
{
	#ifdef NETDEBUG
		printf("%03X> %s\n", data, data_in);
	#endif
	
	ssize_t ret = ::send((SOCKET) data, data_in, length, 0);
	if (ret == length)
		return true;
	
	length = ret;
	return false;
}

//------------------------------------------------------------------------------

bool TCPSocket::recv(char *data_out, size_t &length)
{
	ssize_t ret = ::recv((SOCKET) data, data_out, length, 0);
	if (ret >= 0)
	{
		#ifdef NETDEBUG
			printf("%03X< %s\n", data, data_out);
		#endif
		length = ret;
		return true;
	}
	return false;
}

//------------------------------------------------------------------------------

} // namespace Net

//------------------------------------------------------------------------------