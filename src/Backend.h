#pragma once
#include <map>
#include <string>
#include "fooking.h"
#include "Buffer.h"
#include "EventLoop.h"
#include "Connection.h"

NS_BEGIN

typedef struct{
	unsigned char version;
	unsigned char type;
	unsigned char requestIdB1;
	unsigned char requestIdB0;
	unsigned char contentLengthB1;
	unsigned char contentLengthB0;
	unsigned char paddingLength;
	unsigned char reserved;
}FastCGIHeader;

typedef struct{
	unsigned char roleB1;
	unsigned char roleB0;
	unsigned char flags;
	unsigned char reserved[5];
}FastCGIBeginRequest;

typedef struct {
	unsigned char appStatusB3;
	unsigned char appStatusB2;
	unsigned char appStatusB1;
	unsigned char appStatusB0;
	unsigned char protocolStatus;
	unsigned char reserved[3];
}FastCGIEndRequest;

typedef struct{
	const char*	ptr;
	int		total;
	int		length;
	int		offset;
}FastCGIBody;

enum{
	kStart = 0,
	kName,
	kValueReady,
	kValue,
	kBody
};

//fastcgi backend
class Backend:
	public Object
{
public:
	Backend(EventLoop *loop, Connection *client, Buffer *request);
	~Backend();
public:
	bool				run();
	void				shutdown();
	void				addParam(const char *name, int namelen, const char *value, int vallen);
	void				copyParam(const char *data, int len){ bParams.append(data, len); }
	void				copyParam(const Buffer &params){ bParams.append(params); }
	void				setResponseHandler(const EventHandler &cb){ cbResponse = cb;}
	void				setCloseHandler(const EventHandler &cb){ cbClose = cb;}
	Buffer&				getResponse(){ return mResponse;}
	Connection*			getClient(){ return pClient;}
	void				setClient(Connection *c){ pClient = c;}
public:
	static void			addSharedParam(const char *name, int namelen, const char *value, int vallen);
	static int			makeParam(Buffer &buf, const char *name, int namelen, const char *value, int vallen);
private:
	void				onConnect(Connection *conn);
	void				onMessage(Connection *conn);
	void				onClose(Connection *conn);
	void				onWriteComplete(Connection *conn);
private:
	bool 				request();
	bool 				response();
	FastCGIBody			parse(const char *ptr, int len);
	FastCGIHeader		makeHeader(int type, int requestid, int contentLength, int paddingLength);
	FastCGIBeginRequest	makeBeginRequest(int role, int flag);
private:
	EventLoop*			pLoop;
	Connection*			pClient;
	Connection*			pBackend;
	Buffer*				pRequest;
	Buffer				mResponse;
	Buffer				bParams;
	EventHandler		cbResponse;
	EventHandler		cbClose;
	int					nBackendId;
	int					nWeights;
	unsigned int*		pServerFail;
	static Buffer		bSharedParams;
};
NS_END