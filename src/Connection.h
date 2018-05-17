#pragma once
#include <memory>
#include "fooking.h"
#include "EventLoop.h"
#include "Socket.h"
#include "Buffer.h"

NS_BEGIN

class Connection:
	public Object
{
	friend class Server;
public:
	Connection(EventLoop *loop, int fd = INVALID_SOCKET);
	~Connection();
public:
	int				connectTcp(const char *host, short port);
	int				connectUnix(const char *path);
	int				send(const char *data, int len);
	int				send(Buffer &buffer);
	void			close();
	bool			isConnected() const { return bConnected;}
	bool			isClosed() const { return bClosed; }
	int				fd(){ return sSocket.getFd();}
	char*			host(){ return sHost;}
	int				port(){ return nPort;}
	Socket&			getSocket(){ return sSocket;}
	Buffer*			getBuffer(){return &readBuffer;}
	void			setMessageHandler(const EventHandler &cb){ cbRead = cb;}
	void			setCloseHandler(const EventHandler &cb){ cbClose = cb;}
	void			setConnectHandler(const EventHandler &cb){ cbConnect = cb;}
	void			setWriteCompleteHandler(const EventHandler &cb){ cbWriteComplete = cb;}
	void			setTimeout(int msec){nTimeout = msec;}
	void			setContext(void *context){ pContext = context;}
	void*			getContext() const{ return pContext;}
	void			setHostAndPort(const char *host, short port);
	int				getError(){ return nError;}
	void			setSSL(SSL* ssl){ pSSL = ssl;}
private:
	void			initSocket();
	void			initConnectEvent();
	void			attach();
	void			onConnect(int fd, int r, void *data);
	void			onRead(int fd, int r, void *data);
	void			onWrite(int fd, int r, void *data);
	void			onTimeout(TimerId id, void *data);
private:
	EventLoop*		pEventLoop;
	Socket			sSocket;
	Buffer			writeBuffer;
	Buffer			readBuffer;
	bool			bWriting;
	bool			bClosed;
	bool			bConnected;
	EventHandler	cbRead;
	EventHandler	cbClose;
	EventHandler	cbConnect;
	EventHandler	cbWriteComplete;
	void*			pContext;
	char			sHost[20];
	int				nPort;
	int				nTimeout;
	TimerId			nTimerId;
	int				nError;
	bool			bSSL;
	SSL*			pSSL;
};

NS_END