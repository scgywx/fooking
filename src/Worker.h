#pragma once
#include <string>
#include <map>
#include <list>
#include "Common.h"
#include "Process.h"
#include "EventLoop.h"
#include "Buffer.h"
#include "Connection.h"
#include "Protocol.h"
#include "Server.h"
#include "Session.h"
#include "Router.h"
#include "Hashtable.h"
#include "Script.h"

NS_BEGIN

typedef hash_map<Connection*, int> BackendList;
typedef struct{
	Session		session;
	Buffer		params;
	BackendList	backends;
	ChannelSet	channels;
}ClientData;

typedef struct{
	bool		dParam;//请求结束后是否需要释放params
	bool		connected;
	Connection*	client;
	Buffer*		request;
	Buffer*		params;
}BackendRequest;

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
	void				onBackendConnect(Connection *conn);
	void				onBackendMessage(Connection *conn);
	void				onBackendClose(Connection *conn);
	void				onBackendWriteComplete(Connection *conn);
	//router process handler
	void				onRouterMessage(Connection *conn);
	void				onRouterClose(Connection *conn);
	void				onRouterConnect(Connection *conn);
	//on timer
	void				onTimer(long long id, void *data);
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
	void				sendToRouter(uint_16 type, uint_16 slen, const char *sessptr, int len, const char *dataptr);
	Connection*			createBackendRequest(Connection *client, Buffer *request, Buffer *params, bool dParam = false);
private:
	int					nId;
	Master*				pMaster;
	EventLoop*			pEventLoop;
	ClientList			arrClients;
	IProtocol*			pProtocol;
	Connection*			pRouter;
	Server*				pServer;
	ChannelList			arrChannels;
	int					nRouterReconnect;
	int					nRouterDelay;
	BackendList			arrExpireBackends;
	int					nPerWorkerAcceptMax;
};
NS_END