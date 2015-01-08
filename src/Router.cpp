#include <iostream>
#include "Router.h"
#include "Socket.h"
#include "Log.h"
#include "Utils.h"
#include "Master.h"

NS_USING;

Router::Router(Master *master):
	pMaster(master),
	pEventLoop(NULL)
{
}

Router::~Router()
{
	delete pEventLoop;
}

void Router::onConnection(int fd, int ev, void *data)
{
	LOG("new client");
	Connection *conn = new Connection(pEventLoop, fd);
	conn->setMessageHandler(EV_CB(this, Router::onMessage));
	conn->setCloseHandler(EV_CB(this, Router::onClose));
	
	//info
	GatewayInfo *pInfo = new GatewayInfo();
	pInfo->isauth = false;
	pInfo->serverid = 0;
	conn->setData(pInfo);
	
	allGateways[conn] = 1;
}

void Router::onClose(Connection *conn)
{
	LOG("close client");
	GatewayInfo *pInfo = (GatewayInfo*)conn->getData();
	
	//清理session
	SessionSet::const_iterator it;
	for(it = pInfo->sessions.begin(); it != pInfo->sessions.end(); ++it){
		allSessions.erase(it->first);
	}
	
	//清理channel
	ChannelSet::const_iterator it2;
	for(it2 = pInfo->channels.begin(); it2 != pInfo->channels.end(); ++it2){
		allChannels[it2->first].erase(conn);
	}
	
	//清理gateway
	allGateways.erase(conn);
	
	delete pInfo;
	delete conn;
}

void Router::sendHead(Connection *conn, uint_16 type, uint_16 slen, int len)
{
	RouterMsg msg;
	writeNetInt16((char*)&msg.type, type);
	writeNetInt16((char*)&msg.slen, slen);
	writeNetInt32((char*)&msg.len, len);
	conn->send((char*)&msg, ROUTER_HEAD_SIZE);
}

void Router::getSessionGroups(SessionGroup &groups, int slen, char *sptr)
{
	int pos = 0;
	while(pos + SID_LENGTH <= slen)
	{
		std::string sid(sptr + pos, SID_LENGTH);
		pos+= SID_LENGTH;
		
		Connection *gate = allSessions[sid];
		if(!gate){
			LOG("gate not found by sid=%s", sid.c_str());
			continue;
		}
		
		groups[gate].append(sid);
	}
}

void Router::onMessage(Connection *conn)
{
	Buffer *pBuffer = conn->getBuffer();
	while(true)
	{
		if(pBuffer->size() < ROUTER_HEAD_SIZE){
			return ;
		}
		
		RouterMsg *pMsg = (RouterMsg*)pBuffer->data();
		pMsg->type = readNetInt16((char*)&pMsg->type);
		pMsg->slen = readNetInt16((char*)&pMsg->slen);
		pMsg->len = readNetInt32((char*)&pMsg->len);
		if(pBuffer->size() < ROUTER_HEAD_SIZE + pMsg->slen + pMsg->len){
			return ;
		}
		
		LOG("on data, type=%d, slen=%d, len=%d", pMsg->type, pMsg->slen, pMsg->len);
		
		switch(pMsg->type){
			case ROUTER_MSG_AUTH:
				doAuth(conn, pMsg);
				break;
			case ROUTER_MSG_CONN:
				doConn(conn, pMsg);
				break;
			case ROUTER_MSG_CLOSE:
				doClose(conn, pMsg);
				break;
			case ROUTER_MSG_KICK:
				doKick(conn, pMsg);
				break;
			case ROUTER_MSG_SEND_MSG:
				doSendMsg(conn, pMsg);
				break;
			case ROUTER_MSG_SEND_ALL:
				doSendAllMsg(conn, pMsg);
				break;
			case ROUTER_MSG_CH_ADD:
				doChannelAdd(conn, pMsg);
				break;
			case ROUTER_MSG_CH_DEL:
				doChannelDel(conn, pMsg);
				break;
			case ROUTER_MSG_CH_PUB:
				doChannelPub(conn, pMsg);
				break;
			case ROUTER_MSG_CH_SUB:
				doChannelSub(conn, pMsg);
				break;
			case ROUTER_MSG_CH_UNSUB:
				doChannelUnSub(conn, pMsg);
				break;
			case ROUTER_MSG_INFO:
				doInfo(conn, pMsg);
				break;
			default:
				LOG("[router]error type");
				break;
		}
		
		pBuffer->seek(ROUTER_HEAD_SIZE + pMsg->slen + pMsg->len);
	}
}

void Router::doAuth(Connection *conn, RouterMsg *pMsg)
{
	GatewayInfo *pInfo = (GatewayInfo*)conn->getData();
	int serverid = readNetInt32(pMsg->data);
	pInfo->serverid = serverid;
	pInfo->isauth = true;
	
	LOG("auth %d", serverid);
}

void Router::doConn(Connection *conn, RouterMsg *pMsg)
{
	GatewayInfo *pInfo = (GatewayInfo*)conn->getData();
	int pos = 0;
	while(pos + SID_LENGTH <= pMsg->slen){
		std::string sid(pMsg->data + pos, SID_LENGTH);
		LOG("new connection, sid=%s", sid.c_str());
		
		allSessions[sid] = conn;
		pInfo->sessions[sid] = 1;
		
		pos+= SID_LENGTH;
	}
}

void Router::doClose(Connection *conn, RouterMsg *pMsg)
{
	GatewayInfo *pInfo = (GatewayInfo*)conn->getData();
	int pos = 0;
	while(pos + SID_LENGTH <= pMsg->slen){
		std::string sid(pMsg->data + pos, SID_LENGTH);
		
		LOG("close connection, sid=%s", sid.c_str());
		allSessions.erase(sid);
		pInfo->sessions.erase(sid);
		
		pos+= SID_LENGTH;
	}
}

void Router::doKick(Connection *conn, RouterMsg *pMsg)
{
	LOG("kick");
	
	//session分组
	SessionGroup groups;
	getSessionGroups(groups, pMsg->slen, pMsg->data);
	
	//发送
	for(SessionGroup::const_iterator it = groups.begin(); it != groups.end(); ++it){
		Connection *gate = it->first;
		const std::string &sidlist = it->second;
		sendHead(gate, ROUTER_MSG_KICK, sidlist.size(), 0);
		gate->send(sidlist.c_str(), sidlist.size());
	}
}

void Router::doSendMsg(Connection *conn, RouterMsg *pMsg)
{
	//session分组
	SessionGroup groups;
	getSessionGroups(groups, pMsg->slen, pMsg->data);
	
	//发送
	for(SessionGroup::const_iterator it = groups.begin(); it != groups.end(); ++it){
		Connection *gate = it->first;
		const std::string &sidlist = it->second;
		LOG("send msg to: %s, data=%s", sidlist.c_str());
		sendHead(gate, ROUTER_MSG_SEND_MSG, sidlist.size(), pMsg->len);
		gate->send(sidlist.c_str(), sidlist.size());
		gate->send(pMsg->data + pMsg->slen, pMsg->len);
	}
}

void Router::doSendAllMsg(Connection *conn, RouterMsg *pMsg)
{
	ConnectionSet::const_iterator it;
	for(it = allGateways.begin(); it != allGateways.end(); ++it){
		Connection *gate = it->first;
		GatewayInfo *pInfo = (GatewayInfo*)gate->getData();
		LOG("send allmsg to gateway=%p, clients=%d", gate, pInfo->sessions.size());
		if(!pInfo->sessions.size()){
			continue;
		}
		
		sendHead(gate, ROUTER_MSG_SEND_ALL, 0, pMsg->len);
		gate->send(pMsg->data, pMsg->len);
	}
}

void Router::doChannelAdd(Connection *conn, RouterMsg *pMsg)
{
	//session分组
	SessionGroup groups;
	getSessionGroups(groups, pMsg->slen, pMsg->data);
	
	//频道名称在data域
	for(SessionGroup::const_iterator it = groups.begin(); it != groups.end(); ++it){
		Connection *gate = it->first;
		const std::string &sidlist = it->second;
		
		sendHead(gate, ROUTER_MSG_CH_ADD, sidlist.size(), pMsg->len);
		gate->send(sidlist.c_str(), sidlist.size());
		gate->send(pMsg->data + pMsg->slen, pMsg->len);
	}
}

void Router::doChannelDel(Connection *conn, RouterMsg *pMsg)
{
	//session分组
	SessionGroup groups;
	getSessionGroups(groups, pMsg->slen, pMsg->data);
	
	//频道名称在data域
	for(SessionGroup::const_iterator it = groups.begin(); it != groups.end(); ++it){
		Connection *gate = it->first;
		const std::string &sidlist = it->second;
		
		sendHead(gate, ROUTER_MSG_CH_DEL, sidlist.size(), pMsg->len);
		gate->send(sidlist.c_str(), sidlist.size());
		gate->send(pMsg->data + pMsg->slen, pMsg->len);
	}
}

void Router::doChannelPub(Connection *conn, RouterMsg *pMsg)
{
	//频道名称在session域
	std::string name(pMsg->data, pMsg->slen);
	ChannelList::const_iterator it = allChannels.find(name);
	if(it == allChannels.end()){
		return ;
	}
	
	LOG("send channel start");
	
	const ConnectionSet &conns = it->second;
	ConnectionSet::const_iterator it2;
	for(it2 = conns.begin(); it2 != conns.end(); ++it2){
		Connection *gate = it2->first;
		sendHead(gate, ROUTER_MSG_CH_PUB, pMsg->slen, pMsg->len);
		gate->send(pMsg->data, pMsg->slen + pMsg->len);
		
		LOG("send channel msg, gateway=%p", gate);
	}
}

void Router::doChannelSub(Connection *conn, RouterMsg *pMsg)
{
	LOG("subscribe channel, gate=%p", conn);
	//频道名称在session域
	GatewayInfo *pInfo = (GatewayInfo*)conn->getData();
	std::string name(pMsg->data, pMsg->slen);
	pInfo->channels[name] = 1;
	allChannels[name][conn] = 1;
}

void Router::doChannelUnSub(Connection *conn, RouterMsg *pMsg)
{
	LOG("unsubscribe channel, gate=%p", conn);
	//频道名称在session域
	GatewayInfo *pInfo = (GatewayInfo*)conn->getData();
	std::string name(pMsg->data, pMsg->slen);
	pInfo->channels.erase(name);
	
	ChannelList::iterator it = allChannels.find(name);
	if(it != allChannels.end()){
		it->second.erase(conn);
		if(it->second.empty()){
			LOG("channel is empty, name=%s", name.c_str());
			allChannels.erase(it);
		}
	}
}

void Router::doInfo(Connection *conn, RouterMsg *pMsg)
{
	//开始遍历网关
	Buffer body;
	int ngate = 0;
	for(ConnectionSet::const_iterator it = allGateways.begin();
		it != allGateways.end(); ++it)
	{
		Connection *gate = it->first;
		GatewayInfo *pInfo = (GatewayInfo*)gate->getData();
		if(!pInfo->isauth){
			continue;
		}
		
		char line[8];
		writeNetInt32(line, pInfo->serverid);
		writeNetInt32(line + sizeof(int), pInfo->sessions.size());
		body.append(line, sizeof(line));
		
		ngate++;
	}
	
	//头信息,网关数量与客户端数量
	char head[8];
	writeNetInt32(head, allSessions.size());
	writeNetInt32(head + sizeof(int), ngate);
	
	//发送消息
	sendHead(conn, ROUTER_MSG_INFO, 0, sizeof(head) + body.size());
	conn->send(head, sizeof(head));
	conn->send(body.data(), body.size());
	
	LOG("getinfo datalen=%d, gates=%d", body.size(), ngate);
}

void Router::start()
{
	pEventLoop = new EventLoop();
	
	//create Router server
	short port = pMaster->pConfig->nPort;
	pServer = new Server(pEventLoop);
	pServer->setConnectionHandler(EV_IO_CB(this, Router::onConnection));
	pServer->createTcpServer(port);
	pServer->start();
	
	LOG("router server started, listening port=%d", port);
	
	pEventLoop->run();
}