#include <iostream>
#include "Server.h"
#include "Connection.h"
#include "Log.h"

NS_USING;

Server::Server(EventLoop *loop, int fd):
	pLoop(loop),
	sSocket(fd)
{
	EV_CB_INIT(cbConnection);
}

Server::~Server()
{
}

int Server::createTcpServer(int port)
{
	if(sSocket.create(AF_INET) == SOCKET_ERR){
		return -1;
	}

	//listen
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	if(sSocket.listen((sockaddr *)&sa, sizeof(sa)) == SOCKET_ERR){
		sSocket.close();
		return -1;
	}
	
	//set nonblock
	sSocket.setNonBlock();

	return 0;
}

int Server::createUnixServer(const char *path)
{
#ifdef WIN32
	return -1;
#else
	if(sSocket.create(AF_LOCAL) == SOCKET_ERR){
		return -1;
	}

	//listen
	struct sockaddr_un sa;
	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_LOCAL;
	strncpy(sa.sun_path, path, sizeof(sa.sun_path) - 1);
	if(sSocket.listen((sockaddr*)&sa, sizeof(sa)) == SOCKET_ERR){
		sSocket.close();
		return -1;
	}
	
	//set nonblock
	sSocket.setNonBlock();

    return 0;
#endif
}

void Server::start()
{
	pLoop->addEventListener(sSocket.getFd(), EV_IO_READ, EV_IO_CB(this, Server::onConnection), NULL);
}

void Server::stop()
{
	pLoop->removeEventListener(sSocket.getFd(), EV_IO_ALL);
}

void Server::onConnection(int fd, int ev, void *data)
{
	struct sockaddr_in sa;
	int len = sizeof(sa);
	int conn = sSocket.accept((sockaddr *)&sa, &len);
	if(conn == SOCKET_ERR){
		LOG_ERR("accept fail, errno=%d, error=%s", errno, strerror(errno));
		return ;
	}
	
	EV_INVOKE(cbConnection, conn, 0, &sa);
}