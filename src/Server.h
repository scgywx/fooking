#pragma once
#include "fooking.h"
#include "EventLoop.h"
#include "Socket.h"

NS_BEGIN

class Server:
	public Object
{
public:
	Server(EventLoop *loop, int fd = INVALID_SOCKET);
	~Server();
public:
	int 				createTcpServer(int port);
	int 				createUnixServer(const char *path);
	void				start();
	void				stop();
	void 				setConnectionHandler(const EventHandler &cb){ cbConnection = cb;}
	Socket&				getSocket(){ return sSocket;}
	bool				openSSL(const std::string &cert, const std::string &pkey);
	void				setEventLoop(EventLoop *loop){ pLoop = loop;}
private:
	void 				onConnection(int fd, int ev, void *data);
	void				onSSLHandShake(int fd, int ev, void *data);
private:	
	EventLoop*			pLoop;
	Socket				sSocket;
	EventHandler		cbConnection;
	bool				bSSL;
	SSL_CTX*			pSSLCtx;
};

NS_END