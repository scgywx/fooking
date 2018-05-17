#pragma once
#include <string>
#include <map>
#include <list>
#include "fooking.h"
#include "Process.h"
#include "EventLoop.h"
#include "Buffer.h"
#include "Connection.h"
#include "Server.h"
#include "Session.h"
#include "Hashtable.h"
#include "Router.h"
#include "Backend.h"
#include "Script.h"

NS_BEGIN

typedef struct IdleNode{
	Connection		*conn;
	uint32_t		expire;
	struct IdleNode *prev;
	struct IdleNode *next;
}IdleNode;

typedef hash_map<RequestContext*, int> RequestList;
typedef struct{
	uint32_t	nrequest;
	Session		session;
	Buffer		params;
	RequestList	requests;
	ChannelSet	channels;
	IdleNode*	idle;
}ClientContext;

class Master;
class Worker:
	public Process
{
public:
	typedef hash_map<std::string, Connection*>	ClientList;
public:
	Worker(Master *master, int id);
	~Worker();
public:
	int					id(){return nId;}
protected:
	void 				proc();
private:
	//channel process handler
	void				onPipeMessage(Connection *conn);
	//client process handler
	void				onConnection(Connection *conn);
	void 				onMessage(Connection *conn);
	void 				onClose(Connection *conn);
	//backend process handler
	void				onBackendHandler(RequestContext *ctx);
	//router process handler
	void				onRouterMessage(Connection *conn);
	void				onRouterClose(Connection *conn);
	void				onRouterConnect(Connection *conn);
	void				onRouterReconnect(TimerId id, void *data);
private:
	void				doKick(RouterMsg *pMsg);
	void				doSendMsg(RouterMsg *pMsg);
	void				doSendAllMsg(RouterMsg *pMsg);
	void				doChannelAdd(RouterMsg *pMsg);
	void				doChannelDel(RouterMsg *pMsg);
	void				doChannelPub(RouterMsg *pMsg);
private:
	void				loopBefore(void *data);
	void				initRouter();
	void				createClient(Connection *conn);
	void 				closeClient(Connection *conn);
	void				sendToClient(Connection *conn, const char *data, int len);
	void				sendToClient(Connection *conn, Buffer *msg);
	void				sendToClientByDefault(Connection *conn, const char *data, int len);
	void				sendToClientByScript(Connection *conn, Buffer *msg);
	void				sendToRouter(uint16_t type, uint16_t slen, const char *sessptr, int len, const char *dataptr);
	void				addIdleNode(Connection *conn);
	void				delIdleNode(Connection *conn);
	void				resetIdleNode(Connection *conn);
private:
	int					nId;
	Master*				pMaster;
	EventLoop*			pEventLoop;
	Script*				pScript;
	ClientList			arrClients;
	Connection*			pRouter;
	Server*				pServer;
	Backend*			pBackend;
	ChannelList			arrChannels;
	int					nRouterReconnect;
	bool				bHeldAcceptLock;
	IdleNode*			pIdleHead;
	IdleNode*			pIdleTail;
	Connection*			pPipe;
};
NS_END