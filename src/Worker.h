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

NS_BEGIN

typedef hash_map<Backend*, int> BackendList;
typedef struct{
	int			nrequest;
	Session		session;
	Buffer		params;
	BackendList	backends;
	ChannelSet	channels;
}ClientData;

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
	void				onChannel(int fd, int ev, void *data);
	//client process handler
	void				onConnection(int fd, int ev, void *data);
	void 				onMessage(Connection *conn);
	void 				onClose(Connection *conn);
	//backend process handler
	void				onBackendResponse(Backend *conn);
	void				onBackendClose(Backend *conn);
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
	void				createClient(int fd, const char *ip, int port);
	void 				closeClient(Connection *conn);
	void				sendToClient(Connection *conn, const char *data, int len);
	void				sendToClient(Connection *conn, Buffer *msg);
	void				sendToClientRaw(Connection *conn, const char *data, int len);
	void				sendToClientScript(Connection *conn, Buffer *msg);
	void				sendToRouter(uint_16 type, uint_16 slen, const char *sessptr, int len, const char *dataptr);
private:
	int					nId;
	Master*				pMaster;
	EventLoop*			pEventLoop;
	ClientList			arrClients;
	Connection*			pRouter;
	Server*				pServer;
	ChannelList			arrChannels;
	int					nRouterReconnect;
	BackendList			arrExpireBackends;
	bool				bHeldAcceptLock;
};
NS_END