#include <iostream>
#include "Server.h"
#include "Connection.h"
#include "Log.h"

NS_USING;

Server::Server(EventLoop *loop, int fd):
	pLoop(loop),
	sSocket(fd),
	bSSL(false)
{
	EV_CB_INIT(cbConnection);
}

Server::~Server()
{
	if(bSSL){
		SSL_CTX_free(pSSLCtx);
	}
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

bool Server::openSSL(const std::string &cert, const std::string &pkey)
{
	pSSLCtx = SSL_CTX_new(SSLv23_server_method());
	if(pSSLCtx == NULL){
		return false;
	}
	
	if(SSL_CTX_use_certificate_file(pSSLCtx, cert.c_str(), SSL_FILETYPE_PEM) <= 0){
		return false;
	}
	
	if(SSL_CTX_use_PrivateKey_file(pSSLCtx, pkey.c_str(), SSL_FILETYPE_PEM) <= 0){
		return false;
	}
	
	if(!SSL_CTX_check_private_key(pSSLCtx)){
		return false;
	}
	
	bSSL = true;
	
	return true;
}

void Server::onConnection(int listenfd, int ev, void *data)
{
	struct sockaddr_in sa;
	int len = sizeof(sa);
	int fd = sSocket.accept((sockaddr *)&sa, &len);
	if(fd == SOCKET_ERR){
		LOG_ERR("accept fail, errno=%d, error=%s", errno, strerror(errno));
		return ;
	}
	
	//create connection
	Connection *conn = new Connection(pLoop, fd);
	
	//host and port
	conn->nPort = ntohs(sa.sin_port);
	memset(conn->sHost, 0, sizeof(conn->sHost));
	inet_ntop(AF_INET, &(sa.sin_addr), conn->sHost, sizeof(conn->sHost)); 
	
	if(bSSL){
		SSL *ssl = SSL_new(pSSLCtx);
		SSL_set_fd(ssl, fd);
		conn->pSSL = ssl;
		pLoop->addEventListener(fd, EV_IO_READ, EV_IO_CB(this, Server::onSSLHandShake), conn);
	}else{
		conn->attach();
		EV_INVOKE(cbConnection, conn);
	}
}

void Server::onSSLHandShake(int fd, int ev, void *data)
{
	Connection *conn = static_cast<Connection*>(data);
	if(SSL_accept(conn->pSSL) == -1){
		if(errno == EAGAIN){
			return;
		}
		
		pLoop->removeEventListener(fd, EV_IO_READ);
		LOG_ERR("ssl accept failed, no=%d, str=%s", errno, strerror(errno));
		return;
	}
	
	pLoop->removeEventListener(fd, EV_IO_READ);
	conn->bSSL = true;
	conn->attach();
	EV_INVOKE(cbConnection, conn);
}