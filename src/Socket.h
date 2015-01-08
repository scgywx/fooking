#pragma once
#include "Common.h"
#include "Log.h"

#ifdef WIN32
#include <WinSock2.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#endif

NS_BEGIN

#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1
#endif

#define SOCKET_ERR		-1
#define SOCKET_OK		0

class Socket
{
public:
	Socket():nSocket(INVALID_SOCKET){}
	Socket(int fd):nSocket(fd){}
	~Socket(){}
public:
	int getFd(){ return nSocket; }
	int isValid(){ return nSocket != INVALID_SOCKET;}
	int create(int domain);
	int close();
	int listen(struct sockaddr *sa, int len);
	int accept(sockaddr *addr, int *len);
	int connectTcp(const char *host, short port, int isNonBlock = 0);
	int connectUnix(const char *path, int isNonBlock = 0);
	int read(char *buffer, int len);
	int write(const char *buffer, int len);
	int setReuseAddr();
	int setNonBlock();
	int setNonDelay();
	int setKeepAlive();
private:
	int nSocket;
};

NS_END