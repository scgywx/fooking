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
	void 				setConnectionHandler(const IOHandler &cb){ cbConnection = cb;}
	Socket&				getSocket(){ return sSocket;}
private:
	void 				onConnection(int fd, int ev, void *data);
private:	
	EventLoop*			pLoop;
	Socket				sSocket;
	IOHandler			cbConnection;
};
NS_END