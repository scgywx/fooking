#pragma once
#include "Common.h"
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
	void 				setConnectionHandler(const IOEventHandler &cb){ cbConnection = cb;}
	Socket&				getSocket(){ return sSocket;}
private:
	void				initSocket();
	void 				onConnection(int fd, int ev, void *data);
private:	
	EventLoop*			pLoop;
	Socket				sSocket;
	IOEventHandler		cbConnection;
};
NS_END