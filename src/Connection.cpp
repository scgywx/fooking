#include "Connection.h"
#include "Log.h"

NS_USING;

Connection::Connection(EventLoop *loop, int fd):
	pEventLoop(loop),
	sSocket(fd),
	bWriting(false),
	bClosed(false),
	bConnected(false),
	pContext(NULL),
	nPort(0),
	nTimeout(0),
	nTimerId(0),
	nError(0),
	bSSL(false),
	pSSL(NULL)
{
	EV_CB_INIT(cbRead);
	EV_CB_INIT(cbClose);
	EV_CB_INIT(cbWriteComplete);
	EV_CB_INIT(cbConnect);
	
	memset(sHost, 0, sizeof(sHost));
	
	if(sSocket.isValid()){
		initSocket();
	}
}

Connection::~Connection()
{
	if(bSSL){
		SSL_free(pSSL);
	}
	
	if(!bClosed){
		close();
	}
}

void Connection::attach()
{
	pEventLoop->addEventListener(sSocket.getFd(), EV_IO_READ, EV_IO_CB(this, Connection::onRead), NULL);
}

void Connection::initSocket()
{
	bConnected = true;
	sSocket.setNonBlock();
	sSocket.setNonDelay();
	sSocket.setKeepAlive();
}

void Connection::initConnectEvent()
{
	pEventLoop->addEventListener(sSocket.getFd(), EV_IO_ALL, EV_IO_CB(this, Connection::onConnect), NULL);
	if(nTimeout){
		nTimerId = pEventLoop->setTimer(nTimeout, EV_TIMER_CB(this, Connection::onTimeout));
	}
}

void Connection::setHostAndPort(const char *host, short port)
{
	strcpy(sHost, host);
	nPort = port;
}

int Connection::connectTcp(const char *host, short port)
{
	if(sSocket.connectTcp(host, port, 1) == SOCKET_ERR){
		int err = sSocket.getError();
		LOG_ERR("connect tcp server failure, errno=%d, error=%s", err, strerror(err));
		return -1;
	}
	
	initConnectEvent();
	
	return 0;
}

int Connection::connectUnix(const char *path)
{
	if(sSocket.connectUnix(path, 1) == SOCKET_ERR){
		int err = sSocket.getError();
		LOG_ERR("connect unix domain failure, errno=%d", err, strerror(err));
		return -1;
	}
		
	initConnectEvent();
	
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
	if(bClosed){
		return ;
	}
	
	//remove io event
	pEventLoop->removeEventListener(sSocket.getFd(), EV_IO_ALL);
	
	//remove timer event
	if(nTimerId){
		pEventLoop->stopTimer(nTimerId);
	}
	
	bClosed = true;
	sSocket.close();
	
	EV_INVOKE(cbClose, this);
}

void Connection::onTimeout(TimerId id, void *data)
{
	nError = ETIMEDOUT;
	close();
}

void Connection::onConnect(int fd, int r, void *data)
{
	int sockerr = 0;
	socklen_t errlen = sizeof(sockerr);

	//remove all event
	pEventLoop->removeEventListener(sSocket.getFd(), EV_IO_ALL);
	
	//remove timer
	if(nTimeout){
		pEventLoop->stopTimer(nTimerId);
	}
	
	//check fd
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &sockerr, &errlen) == -1){
		sockerr = errno;
	}
	
	//error
	if(sockerr){
		nError = sockerr;
		close();
		return ;
	}
	
	initSocket();
	attach();
	
	EV_INVOKE(cbConnect, this);
}

void Connection::onRead(int fd, int r, void *data)
{
	char buf[4096];
	int n;
	
	if(bSSL){
		n = SSL_read(pSSL, buf, sizeof(buf));
		if(n == 0){
			n = SOCKET_ERR;
		}else if(n < 0){
			if(errno != EAGAIN){
				n = SOCKET_ERR;
			}
		}
	}else{
		n = sSocket.read(buf, sizeof(buf));
	}
	
	LOG("on read, fd=%d, recv=%d", fd, n);
	if(n == SOCKET_ERR){
		nError = sSocket.getError();
		close();
	}else if(n > 0){
		readBuffer.append(buf, n);
		EV_INVOKE(cbRead, this);
	}
}

void Connection::onWrite(int fd, int r, void *data)
{
	char *buffer = writeBuffer.data();
	size_t size = writeBuffer.size();
	int n;
	
	if(bSSL){
		n = SSL_write(pSSL, buffer, size);
		if(n == 0){
			n = SOCKET_ERR;
		}else if(n < 0){
			if(errno != EAGAIN){
				n = SOCKET_ERR;
			}
		}
	}else{
		n = sSocket.write(buffer, size);
	}
	
	LOG("write to fd=%d, len=%d", fd, n);
	if(n == SOCKET_ERR){
		nError = sSocket.getError();
		close();
	}else if(n > 0){
		int remain = writeBuffer.seek(n);
		if(remain <= 0){
			LOG("write completed");
			bWriting = false;
			pEventLoop->removeEventListener(sSocket.getFd(), EV_IO_WRITE);
			EV_INVOKE(cbWriteComplete, this);
		}
	}
}