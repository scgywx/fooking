#include "Socket.h"

NS_USING;

int Socket::create(int domain)
{
	nSocket = socket(domain, SOCK_STREAM, 0);
	if(nSocket == -1){
		return SOCKET_ERR;
	}
	
	if(setReuseAddr() == SOCKET_ERR){
		close();
		return SOCKET_ERR;
	}
	
	return SOCKET_OK;
}

int Socket::close()
{
	::close(nSocket);
	return SOCKET_OK;
}

int Socket::listen(struct sockaddr *sa, int len)
{
	if(::bind(nSocket, sa, len) == -1) {
		return SOCKET_ERR;
	}

	if(::listen(nSocket, 511) == -1) {
		return SOCKET_ERR;
	}

	return SOCKET_OK;
}

int Socket::accept(sockaddr *addr, int *len)
{
	int conn;
	for(;;){
		conn = ::accept(nSocket, addr, (socklen_t*)len);
		if (conn == -1) {
			if (errno == EINTR){
				continue;
			}else {
				return SOCKET_ERR;
			}
		}
		break;
	}

	return conn;
}

int Socket::connectTcp(const char *host, short port, int isNonBlock)
{
	if(create(AF_INET) == SOCKET_ERR){
		return SOCKET_ERR;
	}
	
	if(isNonBlock && setNonBlock() == SOCKET_ERR){
		close();
		return SOCKET_ERR;
	}
	
	struct sockaddr_in s_add;
	bzero(&s_add, sizeof(struct sockaddr_in));
	s_add.sin_family = AF_INET;
	s_add.sin_addr.s_addr = inet_addr(host);
	s_add.sin_port = htons(port);
	if(connect(nSocket, (struct sockaddr *)(&s_add), sizeof(struct sockaddr)) == -1){
		if(errno != EINPROGRESS){
			close();
			nError = errno;
			return SOCKET_ERR;
		}
	}
	
	return SOCKET_OK;
}

int Socket::connectUnix(const char *path, int isNonBlock)
{
	struct sockaddr_un sa;

	if(create(AF_LOCAL) == SOCKET_ERR){
		return SOCKET_ERR;
	}
	
	if(isNonBlock && setNonBlock() == SOCKET_ERR){
		close();
		return SOCKET_ERR;
	}

	sa.sun_family = AF_LOCAL;
	strncpy(sa.sun_path, path, sizeof(sa.sun_path)-1);
	if(connect(nSocket, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
		if(errno != EINPROGRESS){
			close();
			nError = errno;
			return SOCKET_ERR;
		}
	}
	
	return SOCKET_OK;
}

int Socket::read(char *buffer, int len)
{
	int nread = ::read(nSocket, buffer, len);
	if(nread == -1){
		if(errno == EAGAIN){
			return 0;
		}
		nError = errno;
		return SOCKET_ERR;
	}else if(nread == 0){
		nError = 0;
		return SOCKET_ERR;
	}

	return nread;
}

int Socket::write(const char *buffer, int len)
{
	int nwrote = ::write(nSocket, buffer, len);
	if(nwrote == -1){
		if(errno == EAGAIN){
			return 0;
		}
		
		nError = errno;
		return SOCKET_ERR;
	}else if(nwrote == 0){
		nError = 0;
		return SOCKET_ERR;
	}

	return nwrote;
}

int Socket::setReuseAddr()
{
	int yes = 1;
	/* Make sure connection-intensive things like the redis benckmark
	* will be able to close/open sockets a zillion of times */
	if (setsockopt(nSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
		return SOCKET_ERR;
	}
	return SOCKET_OK;
}

int Socket::setNonBlock()
{
	int flags;

	/* Set the socket non-blocking.
	 * Note that fcntl(2) for F_GETFL and F_SETFL can't be
	 * interrupted by a signal. */
	if ((flags = fcntl(nSocket, F_GETFL)) == -1) {
		return SOCKET_ERR;
	}
	if (fcntl(nSocket, F_SETFL, flags | O_NONBLOCK) == -1) {
		return SOCKET_ERR;
	}

	return SOCKET_OK;
}

int Socket::setNonDelay()
{
	int val = 1;
	
	if (setsockopt(nSocket, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) == -1)
	{
		return SOCKET_ERR;
	}
	
	return SOCKET_OK;
}

int Socket::setKeepAlive()
{
	int val = 1;

	if (setsockopt(nSocket, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == -1)
	{
		return SOCKET_ERR;
	}
	
	return SOCKET_OK;
}