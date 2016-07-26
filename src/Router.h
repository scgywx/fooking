#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>
#include "fooking.h"
#include "Process.h"
#include "EventLoop.h"
#include "Buffer.h"
#include "Server.h"
#include "Connection.h"
#include "Hashtable.h"

NS_BEGIN

#define ROUTER_MSG_AUTH			1
#define ROUTER_MSG_CONN			2
#define ROUTER_MSG_CLOSE		3
#define ROUTER_MSG_KICK			4
#define ROUTER_MSG_SEND_MSG		5
#define ROUTER_MSG_SEND_ALL		6
#define ROUTER_MSG_CH_ADD		7
#define ROUTER_MSG_CH_DEL		8
#define ROUTER_MSG_CH_PUB		9
#define ROUTER_MSG_CH_SUB		10
#define ROUTER_MSG_CH_UNSUB		11
#define ROUTER_MSG_INFO			12

#define ROUTER_HEAD_SIZE		(sizeof(RouterMsg))

typedef struct{
	uint16_t	type;//类型
	uint16_t	slen;//session长度
	int			len;//数据长度
	char		data[0];
}RouterMsg;

typedef hash_map<Connection*, int> ConnectionSet;
typedef hash_map<std::string, int> ChannelSet;
typedef hash_map<std::string, int> SessionSet;
typedef hash_map<std::string, Connection*>	SessionList;
typedef hash_map<std::string, ConnectionSet> ChannelList;

typedef struct{
	bool		isauth;
	int			serverid;
	int			workerid;
	SessionSet	sessions;//负责的连接
	ChannelSet	channels;//订阅的频道
}GatewayInfo;

class Router:
	public Object
{
public:
	typedef hash_map<Connection*, std::string>	SessionGroup;
public:
	Router(int argc, char **argv);
	~Router();
public:
	void start();
	static RouterMsg unpackMsg(void *ptr);
	static void packMsg(void *ptr, uint16_t type, uint16_t slen, int len);
private:
	void onConnection(int fd, int ev, void *data);
	void onMessage(Connection *conn);
	void onClose(Connection *conn);
	void sendHead(Connection *conn, uint16_t type, uint16_t slen, int len);
	void getSessionGroups(SessionGroup &groups, int slen, char *sptr);
private:
	void doAuth(Connection *conn, RouterMsg *pMsg);
	void doConn(Connection *conn, RouterMsg *pMsg);
	void doClose(Connection *conn, RouterMsg *pMsg);
	void doKick(Connection *conn, RouterMsg *pMsg);
	void doSendMsg(Connection *conn, RouterMsg *pMsg);
	void doSendAllMsg(Connection *conn, RouterMsg *pMsg);
	void doChannelAdd(Connection *conn, RouterMsg *pMsg);
	void doChannelDel(Connection *conn, RouterMsg *pMsg);
	void doChannelPub(Connection *conn, RouterMsg *pMsg);
	void doChannelSub(Connection *conn, RouterMsg *pMsg);
	void doChannelUnSub(Connection *conn, RouterMsg *pMsg);
	void doInfo(Connection *conn, RouterMsg *pMsg);
private:
	int					nArgc;
	char**				pArgv;
	EventLoop*			pEventLoop;
	Server*				pServer;
	SessionList			allSessions;//所有连接
	ChannelList			allChannels;//所有频道
	ConnectionSet		allGateways;//所有网关
};
NS_END