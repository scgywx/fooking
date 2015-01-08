#pragma once
#include "Common.h"
#include "EventLoop.h"
#include "Socket.h"
#include "Buffer.h"

NS_BEGIN

class Connection:
	public Object
{
public:
	Connection(EventLoop *loop, int fd = INVALID_SOCKET);
	~Connection();
public:
	int				connectTcp(const char *host, short port);
	int				connectUnix(const char *path);
	int				send(const char *data, int len);
	int				send(Buffer &buffer);
	void			close();
	Socket&			getSocket(){ return sSocket;}
	Buffer*			getBuffer(){return &readBuffer;}
	void			setMessageHandler(const EventHandler &cb){ cbReadHandler = cb;}
	void			setCloseHandler(const EventHandler &cb){ cbCloseHandler = cb;}
	void			setConnectHandler(const EventHandler &cb){ cbConnectHandler = cb;}
	void			setWriteCompleteHandler(const EventHandler &cb){ cbWriteCompleteHandler = cb;}
	void			setData(void *data){ pData = data;}
	void*			getData() const{ return pData;}
	void			setIPAndPort(const char *ip, short port);
private:
	void			onConnect(int fd, int r, void *data);
	void			onRead(int fd, int r, void *data);
	void			onWrite(int fd, int r, void *data);
	void			onClose();
private:
	EventLoop*		pEventLoop;
	Socket			sSocket;
	Buffer			writeBuffer;
	Buffer			readBuffer;
	bool			bWriting;
	EventHandler	cbReadHandler;
	EventHandler	cbCloseHandler;
	EventHandler	cbConnectHandler;
	EventHandler	cbWriteCompleteHandler;
	void*			pData;
	bool			bShutdown;
	char			sIP[16];
	short			nPort;
};

NS_END