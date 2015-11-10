#include <iostream>
#include "Router.h"
#include "Socket.h"
#include "Log.h"
#include "Config.h"
#include "Utils.h"

NS_USING;

RouterMsg Router::unpackMsg(void *ptr)
{
	RouterMsg *pMsg = (RouterMsg*)ptr;
	RouterMsg msg;
	
	msg.type = utils::readNetInt16((char*)&pMsg->type);
	msg.slen = utils::readNetInt16((char*)&pMsg->slen);
	msg.len = utils::readNetInt32((char*)&pMsg->len);
	
	return msg;
}

void Router::packMsg(void *ptr, uint16_t type, uint16_t slen, int len)
{
	RouterMsg *pMsg = (RouterMsg*)ptr;
	utils::writeNetInt16((char*)&pMsg->type, type);
	utils::writeNetInt16((char*)&pMsg->slen, slen);
	utils::writeNetInt32((char*)&pMsg->len, len);
}

Router::Router(int argc, char **argv):
	nArgc(argc),
	pArgv(argv),
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

void Router::sendHead(Connection *conn, uint16_t type, uint16_t slen, int len)
{
	RouterMsg msg;
	packMsg(&msg, type, slen, len);
	conn->send((char*)&msg, ROUTER_HEAD_SIZE);
}

void Router::getSessionGroups(SessionGroup &groups, int slen, char *sptr)
{
	int pos = 0;
	while(pos + SID_LENGTH <= slen)
	{
		std::string sid(sptr + pos, SID_LENGTH);
		pos+= SID_LENGTH;
		
		SessionList::const_iterator it = allSessions.find(sid);
		if(it != allSessions.end()){
			Connection *gate = it->second;
			groups[gate].append(sid);
		}else{
			LOG("gate not found by sid=%s", sid.c_str());
			continue;
		}
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
		
		RouterMsg msg = unpackMsg(pBuffer->data());
		if(pBuffer->size() < ROUTER_HEAD_SIZE + msg.slen + msg.len){
			return ;
		}
		
		RouterMsg *pMsg = (RouterMsg*)pBuffer->data();
		pMsg->type = msg.type;
		pMsg->slen = msg.slen;
		pMsg->len = msg.len;
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
	int serverid = utils::readNetInt32(pMsg->data);
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
	Buffer body;
	char buffer[1024];
	int bufflen;
	
	//clients
	bufflen = snprintf(buffer, 1024, "clients: %d\r\n", allSessions.size());
	body.append(buffer, bufflen);
	
	//channels
	bufflen = snprintf(buffer, 1024, "channels: %d\r\n", allChannels.size());
	body.append(buffer, bufflen);
	
	//gateways
	for(ConnectionSet::const_iterator it = allGateways.begin();
		it != allGateways.end(); ++it)
	{
		Connection *gate = it->first;
		GatewayInfo *pInfo = (GatewayInfo*)gate->getData();
		if(!pInfo->isauth){
			continue;
		}
		
		bufflen = snprintf(buffer, 1024, "gateway: %d\t%d\t%d\r\n", 
				pInfo->serverid, pInfo->sessions.size(), pInfo->channels.size());
		body.append(buffer, bufflen);
	}
	
	//发送消息
	sendHead(conn, ROUTER_MSG_INFO, 0, body.size());
	conn->send(body.data(), body.size());
}

void Router::start()
{
	pEventLoop = new EventLoop();
	
	//create Router server
	Config *pConfig = Config::getInstance();
	pServer = new Server(pEventLoop);
	pServer->setConnectionHandler(EV_IO_CB(this, Router::onConnection));
	if(pServer->createTcpServer(pConfig->nPort) != 0){
		LOG("create router server failed, errno=%d, error=%s", errno, strerror(errno));
		return;
	}
	
	//set title
	char title[256];
	snprintf(title, 256, "fooking router server, %s", pConfig->sFile.c_str());
	utils::setProcTitle(title);

	//start
	LOG("router server started, listening port=%d", pConfig->nPort);
	pServer->start();
	pEventLoop->run();
}
