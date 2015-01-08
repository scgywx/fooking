#include "Connection.h"
#include "Log.h"

NS_USING;

Connection::Connection(EventLoop *loop, int fd):
	pEventLoop(loop),
	sSocket(fd),
	bWriting(false),
	pData(NULL),
	bShutdown(false),
	nPort(0)
{
	EV_CB_INIT(cbReadHandler);
	EV_CB_INIT(cbCloseHandler);
	EV_CB_INIT(cbWriteCompleteHandler);
	EV_CB_INIT(cbConnectHandler);
	
	memset(sIP, 0, sizeof(sIP));
	
	if(sSocket.isValid()){
		sSocket.setNonBlock();
		sSocket.setNonDelay();
		sSocket.setKeepAlive();
		pEventLoop->addEventListener(sSocket.getFd(), EV_IO_READ, EV_IO_CB(this, Connection::onRead), NULL);
	}
}

Connection::~Connection()
{
}

void Connection::setIPAndPort(const char *ip, short port)
{
	strcpy(sIP, ip);
	nPort = port;
}

int Connection::connectTcp(const char *host, short port)
{
	if(sSocket.connectTcp(host, port, 1) == SOCKET_ERR){
		return -1;
	}
	
	pEventLoop->addEventListener(sSocket.getFd(), EV_IO_ALL, EV_IO_CB(this, Connection::onConnect), NULL);
	
	return 0;
}

int Connection::connectUnix(const char *path)
{
	if(sSocket.connectUnix(path) == SOCKET_ERR){
		return -1;
	}
	
	pEventLoop->addEventListener(sSocket.getFd(), EV_IO_ALL, EV_IO_CB(this, Connection::onConnect), NULL);
	
	return 0;
}

int Connection::send(const char *data, int len)
{
	writeBuffer.append(data, len);
	if(!bWriting){
		bWriting = true;
		pEventLoop->addEventListener(sSocket.getFd(), EV_IO_WRITE, EV_IO_CB(this, Connection::onWrite), NULL);
	}
	return 0;
}

int Connection::send(Buffer &buffer)
{
	return send(buffer.data(), buffer.size());
}

void Connection::close()
{
	if(bShutdown || !sSocket.isValid()){
		return ;
	}
	
	bShutdown = true;
	pEventLoop->removeEventListener(sSocket.getFd(), EV_IO_ALL);
	sSocket.close();
	
	pEventLoop->nextTick(EV_CB(this, Connection::onClose), NULL);
}

void Connection::onClose()
{
	EV_INVOKE(cbCloseHandler, this);
}

void Connection::onConnect(int fd, int r, void *data)
{
	int sockerr = 0;
	socklen_t errlen = sizeof(sockerr);

	//remove all event
	pEventLoop->removeEventListener(sSocket.getFd(), EV_IO_ALL);
	
	//check fd
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &sockerr, &errlen) == -1){
		sockerr = errno;
	}
	
	//error
	if(sockerr){
		close();
		return ;
	}
	
	//set socket option
	sSocket.setNonDelay();
	sSocket.setKeepAlive();
	
	pEventLoop->addEventListener(sSocket.getFd(), EV_IO_READ, EV_IO_CB(this, Connection::onRead), NULL);
	
	EV_INVOKE(cbConnectHandler, this);
}

void Connection::onRead(int fd, int r, void *data)
{
	char buf[1024];
	int n = sSocket.read(buf, 1024);
	if(n == SOCKET_ERR){
		close();
	}else if(n > 0){
		readBuffer.append(buf, n);
		EV_INVOKE(cbReadHandler, this);
	}
}

void Connection::onWrite(int fd, int r, void *data)
{
	char *buffer = writeBuffer.data();
	size_t size = writeBuffer.size();
	int n = sSocket.write(buffer, size);
	LOG("write to fd=%d, len=%d", fd, n);
	if(n == SOCKET_ERR){
		close();
	}else if(n > 0){
		int remain = writeBuffer.seek(n);
		if(remain <= 0){
			LOG("write completed");
			bWriting = false;
			pEventLoop->removeEventListener(sSocket.getFd(), EV_IO_WRITE);
			EV_INVOKE(cbWriteCompleteHandler, this);
		}
	}
}