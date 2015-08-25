#pragma once
#include <map>
#include <string>
#include <list>
#include <vector>
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

typedef struct{
	int serverid;
	TimerId timer;
	Connection *backend;
	Connection *client;
	Buffer *req;
	Buffer *rep;
	Buffer *params;
	uint8_t *fails;
	uint8_t abort;
	uint16_t index;
}RequestContext;

//fastcgi backend
class Backend:
	public Object
{
	typedef std::vector<int> ServerRoll;
public:
	Backend(EventLoop *loop);
	~Backend();
public:
	RequestContext*		post(Connection *client, Buffer *body, Buffer *params);
	void				abort(RequestContext *ctx){ ctx->abort = 1; }
	void				addParam(const char *name, int namelen, const char *value, int vallen);
	void				setHandler(const EventHandler &cb){ cbHandler = cb; }
public:
	static int			makeParam(Buffer &buf, const char *name, int namelen, const char *value, int vallen);
private:
	void				onConnect(Connection *conn);
	void				onMessage(Connection *conn);
	void				onClose(Connection *conn);
	void				onWriteComplete(Connection *conn);
	void				onTimeout(TimerId id, Connection *conn);
private:
	bool				connect(RequestContext *ctx);
	bool 				request(RequestContext *ctx);
	bool 				response(RequestContext *ctx);
	FastCGIBody			parse(const char *ptr, int len);
	FastCGIHeader		makeHeader(int type, int requestid, int contentLength, int paddingLength);
	FastCGIBeginRequest	makeBeginRequest(int role, int flag);
private:
	EventLoop*			pLoop;
	EventHandler		cbHandler;
	Buffer				bParams;
	ServerRoll			arrServerRolls;
	int					nRoll;
	int					nServers;
	int					nKeepalive;
	int					nConnectTimeout;
	int					nReadTimeout;
	int					nIdleTop;
	RequestContext**	arrIdleBackends;
};
NS_END