#include <iostream>
#include <signal.h>
#include "Worker.h"
#include "Socket.h"
#include "Master.h"
#include "Router.h"
#include "Atomic.h"
#include "Log.h"
#include "Config.h"
#include "Utils.h"

NS_USING;

Worker::Worker(Master *master, int id):
	nId(id),
	pMaster(master),
	pEventLoop(NULL),
	nRouterReconnect(0),
	bHeldAcceptLock(false),
	pIdleHead(NULL),
	pIdleTail(NULL)
{
	//TODO
}

Worker::~Worker()
{
	//TODO
}

void Worker::createClient(int fd, const char *host, int port)
{
	//计数更新
	pMaster->addClient(nId);
	
	//创建session对像
	Config *cc = Config::getInstance();
	Session sess(pMaster->pGlobals->count);
	LOG("new client, fd=%d, host=%s, port=%d, sid=%s", fd, host, port, sess.getId());
	
	//环镜变量
	ClientData *pData = new ClientData();
	char szPort[8];
	int nPort = sprintf(szPort, "%d", port);
	pData->session = sess;
	pData->nrequest = 0;
	std::string env(cc->sFastcgiPrefix);
	env+= "SESSIONID";
	Backend::makeParam(pData->params, "REMOTE_ADDR", sizeof("REMOTE_ADDR") - 1, host, strlen(host));
	Backend::makeParam(pData->params, "REMOTE_PORT", sizeof("REMOTE_PORT") - 1, szPort, nPort);
	Backend::makeParam(pData->params, env.c_str(), env.size(), sess.getId(), SID_LENGTH);
	
	//创建连接对像
	Connection *client = new Connection(pEventLoop, fd);
	client->setHostAndPort(host, port);
	client->setData(pData);
	client->setMessageHandler(EV_CB(this, Worker::onMessage));
	client->setCloseHandler(EV_CB(this, Worker::onClose));
	//client->setWriteCompleteHandler(EL_CB(this, Worker::onWriteComplete));
	
	//客户端列表更新
	arrClients[sess.getId()] = client;
	
	//Router通知
	sendToRouter(ROUTER_MSG_CONN, SID_LENGTH, sess.getId(), 0, NULL);
	
	//backend通知
	if(cc->bEventConnect){
		Buffer *params = new Buffer(pData->params);
		std::string env(cc->sFastcgiPrefix);
		env+= "EVENT";
		
		Backend::makeParam(*params, env.c_str(), env.size(), "1", 1);
		if(!pBackend->post(NULL, NULL, params)){
			delete params;
		}
	}
	
	//lua通知
	if(pScript && pScript->hasConnectProc()){
		pScript->procConnect(client);
	}
	
	//idle添加
	if(cc->nIdleTime > 0){
		addIdleNode(client);
	}
}

void Worker::closeClient(Connection *client)
{
	ClientData *pData = static_cast<ClientData*>(client->getData());
	std::string sid = pData->session.getId();
	LOG("close client, fd=%d, sid=%s", client->getSocket().getFd(), sid.c_str());
	
	//router通知
	sendToRouter(ROUTER_MSG_CLOSE, SID_LENGTH, pData->session.getId(), 0, NULL);
	
	//后端通知
	Config *cc = Config::getInstance();
	if(cc->bEventClose){
		Buffer *params = new Buffer(pData->params);
		std::string env(cc->sFastcgiPrefix);
		env+= "EVENT";
		
		Backend::makeParam(*params, env.c_str(), env.size(), "2", 1);
		if(!pBackend->post(NULL, NULL, params)){
			delete params;
		}
	}
	
	//释放后端请求
	for(RequestList::const_iterator it = pData->requests.begin(); it != pData->requests.end(); ++it){
		pBackend->abort(it->first);
	}
	
	//取消频道订阅
	for(ChannelSet::const_iterator it2 = pData->channels.begin(); it2 != pData->channels.end(); ++it2){
		const std::string &chname = it2->first;
		
		ChannelList::iterator cit = arrChannels.find(chname);
		if(cit != arrChannels.end()){
			cit->second.erase(client);
			if(cit->second.empty()){
				sendToRouter(ROUTER_MSG_CH_UNSUB, chname.size(), chname.c_str(), 0, NULL);
				arrChannels.erase(cit);
			}
		}
	}
	
	//计数更新
	pMaster->delClient(nId);
	
	//客户端列表更新
	arrClients.erase(sid);
	
	//lua更新
	if(pScript && pScript->hasCloseProc()){
		pScript->procClose(client);
	}
	
	//删除idle
	if(cc->nIdleTime > 0){
		delIdleNode(client);
	}
	
	delete pData;
	delete client;
}

void Worker::sendToRouter(uint16_t type, uint16_t slen, const char *sessptr, int len, const char *dataptr)
{
	if(pRouter){
		RouterMsg msg;
		utils::writeNetInt16((char*)&msg.type, type);
		utils::writeNetInt16((char*)&msg.slen, slen);
		utils::writeNetInt32((char*)&msg.len, len);
		pRouter->send((char*)&msg, ROUTER_HEAD_SIZE);
		if(slen){
			pRouter->send(sessptr, slen);
		}
		if(len){
			pRouter->send(dataptr, len);
		}
	}
}

void Worker::sendToClientByDefault(Connection *conn, const char *data, int len)
{
	if(len){
		char hdr[4];
		utils::writeNetInt32(hdr, len);
		conn->send(hdr, 4);
		conn->send(data, len);
	}
}

void Worker::sendToClientByScript(Connection *conn, Buffer *msg)
{
	ClientData *pData = static_cast<ClientData*>(conn->getData());
	Buffer resp;
	int ret = pScript->procWrite(conn, pData->nrequest, msg, &resp);
	if(ret > 0){
		if(!resp.empty()){
			conn->send(resp.data(), resp.size());
		}
	}else if(ret == 0){
		if(!msg->empty()){
			sendToClientByDefault(conn, msg->data(), msg->size());
		}
	}
}

void Worker::sendToClient(Connection *conn, Buffer *msg)
{
	if(pScript && pScript->hasWriteProc()){
		sendToClientByScript(conn, msg);
	}else if(!msg->empty()){
		sendToClientByDefault(conn, msg->data(), msg->size());
	}
}

void Worker::sendToClient(Connection *conn, const char *data, int len)
{
	if(pScript && pScript->hasWriteProc()){
		Buffer msg(data, len);
		sendToClientByScript(conn, &msg);
	}else if(len){
		sendToClientByDefault(conn, data, len);
	}
}

void Worker::onPipeMessage(Connection *conn)
{
	Buffer *pBuffer = conn->getBuffer();
	Config *cc = Config::getInstance();
	
	LOG("on pipe message");
	
	if(pBuffer->size() < sizeof(PipeMsg)){
		LOG("pipe head not enough");
		return ;
	}
	
	PipeMsg *pMsg = (PipeMsg*)pBuffer->data();
	if(pBuffer->size() < pMsg->len + sizeof(PipeMsg)){
		LOG("pipe body not enough");
		return ;
	}
	
	switch(pMsg->type){
		case CH_RELOAD:
			LOG("pipe msg reload");
			break;
		case CH_EXIT:
			LOG("pipe msg exit");
			pEventLoop->stop();
			break;
		default:
			LOG("invalid pipe type");
			break;
	}
	
	pBuffer->seek(sizeof(PipeMsg) + pMsg->len);
}

void Worker::onConnection(int fd, int ev, void *data)
{
	//check maxclients
	Config *pConfig = Config::getInstance();
	if(pConfig->nMaxClients && pMaster->pGlobals->clients >= pConfig->nMaxClients){
		LOG("Connection is full");
		close(fd);
		return ;
	}
	
	//ip and port
	struct sockaddr_in *pAddr = (struct sockaddr_in*)data;
	char ip[17]; 
	int port = ntohs(pAddr->sin_port);
	memset(ip, 0, sizeof(ip));
	inet_ntop(AF_INET, &pAddr->sin_addr, ip, 16);
	
	//create client
	createClient(fd, ip, port);
	
	//free accept lock
	if(pMaster->bUseAcceptMutex && 
		UnLockAcceptMutex(&pMaster->pGlobals->lock, nPid))
	{
		//LOG("unlock accept mutex");
		bHeldAcceptLock = false;
		pServer->stop();
	}
}

void Worker::onClose(Connection *client)
{
	closeClient(client);
}

void Worker::onMessage(Connection *client)
{
	Buffer *pBuffer = client->getBuffer();
	Config *pConfig = Config::getInstance();
	ClientData *pData = static_cast<ClientData*>(client->getData());
	size_t maxSize = static_cast<size_t>(pConfig->nMaxBufferSize);
	
	LOG("client message, fd=%d, len=%d", client->getSocket().getFd(), pBuffer->size());
	
	//check recv buffer size
	if(maxSize && pBuffer->size() > maxSize){
		LOG_INFO("client recv buffer size less");
		client->close();
		return ;
	}
	
	while(pBuffer->size())
	{
		bool proc = false;
		Buffer *msg = new Buffer();
		
		//自定义协议处理
		if(pScript && pScript->hasReadProc()){
			int ret = pScript->procRead(client, pData->nrequest, pBuffer, msg);
			if(ret < 0){
				delete msg;
				return ;
			}else if(ret > 0){
				proc = true;
			}
		}
		
		//原生协议处理
		if(!proc){
			//check head
			size_t hdrSize = 4;
			if(pBuffer->size() < hdrSize){
				LOG("message head not enough");
				delete msg;
				return ;
			}
			
			//check buffer size
			size_t msgSize = utils::readNetInt32(pBuffer->data());
			if(maxSize && msgSize > maxSize){
				LOG_INFO("message body size too large");
				client->close();
				return ;
			}
			
			//check body
			if(pBuffer->size() - msgSize < hdrSize){
				LOG("package length not enough, msgSize=%u, buffSize=%u", msgSize, pBuffer->size());
				delete msg;
				return ;
			}
			
			msg->append(pBuffer->data() + hdrSize, msgSize);
			
			pBuffer->seek(hdrSize + msgSize);
		}
	
		pData->nrequest++;
		LOG("process message, fd=%d, reqid=%d, proc=%d, buffer size=%d, msg len=%d", 
			client->getSocket().getFd(), 
			pData->nrequest,
			proc, 
			pBuffer->size(),
			msg->size());
	
		//create request
		if(!msg->empty()){
			RequestContext *ctx = pBackend->post(client, msg, &pData->params);
			if(ctx){
				pData->requests[ctx] = 1;
			}else{
				delete msg;
			}
		}else{
			delete msg;
		}
		
		//重置idle
		if(pConfig->nIdleTime > 0){
			resetIdleNode(client);
		}
	}
}

void Worker::onBackendHandler(RequestContext *ctx)
{
	LOG("backend response");
	
	Connection *client = ctx->client;
	if(client){
		if(!ctx->abort){
			//unbinding request
			ClientData *pdata = static_cast<ClientData*>(client->getData());
			pdata->requests.erase(ctx);
			
			//send response
			if(ctx->rep && !ctx->rep->empty()){
				sendToClient(client, ctx->rep);
			}
		}
		
		delete ctx->req;
	}else{
		delete ctx->params;
	}
}

void Worker::onRouterMessage(Connection *router)
{
	Buffer *pBuffer = router->getBuffer();
	while(pBuffer->size())
	{
		if(pBuffer->size() < ROUTER_HEAD_SIZE){
			return ;
		}
		
		RouterMsg msg = Router::unpackMsg(pBuffer->data());
		if(pBuffer->size() < ROUTER_HEAD_SIZE + msg.slen + msg.len){
			return ;
		}
		
		RouterMsg *pMsg = (RouterMsg*)pBuffer->data();
		pMsg->type = msg.type;
		pMsg->slen = msg.slen;
		pMsg->len = msg.len;
		LOG("on router data,type=%d, slen=%d, len=%d", pMsg->type, pMsg->slen, pMsg->len);
		
		switch(pMsg->type){
			case ROUTER_MSG_KICK:
				doKick(pMsg);
				break;
			case ROUTER_MSG_SEND_MSG:
				doSendMsg(pMsg);
				break;
			case ROUTER_MSG_SEND_ALL:
				doSendAllMsg(pMsg);
				break;
			case ROUTER_MSG_CH_ADD:
				doChannelAdd(pMsg);
				break;
			case ROUTER_MSG_CH_DEL:
				doChannelDel(pMsg);
				break;
			case ROUTER_MSG_CH_PUB:
				doChannelPub(pMsg);
				break;
			default:
				LOG("invalid type");
				break;
		}
		
		pBuffer->seek(ROUTER_HEAD_SIZE + pMsg->slen + pMsg->len);
	}
}

void Worker::onRouterClose(Connection *router)
{
	int err = router->getError();
	if(err){
		LOG_ERR("router errno=%d, error=%s", err, strerror(err));
	}else{
		LOG_ERR("router close");
	}
	
	delete pRouter;
	pRouter = NULL;
	
	++nRouterReconnect;
	LOG("reconnect router after %d seconds", nRouterReconnect * 3);
	pEventLoop->setTimer(nRouterReconnect * 3000, EV_TIMER_CB(this, Worker::onRouterReconnect));
}

void Worker::onRouterReconnect(TimerId id, void *data)
{
	initRouter();
}

void Worker::onRouterConnect(Connection *router)
{
	LOG_INFO("router connected");
	
	nRouterReconnect = 0;
	
	//AUTH
	Config *pConfig = Config::getInstance();
	char authstr[8];
	utils::writeNetInt32(authstr, pConfig->nServerId);
	utils::writeNetInt32(authstr + sizeof(int), nId);
	sendToRouter(ROUTER_MSG_AUTH, 0, NULL, sizeof(authstr), authstr);
	
	//sync session
	int sidnum = 0;
	std::string sidlist;
	sidlist.reserve(arrClients.size() * SID_LENGTH);
	for(ClientList::const_iterator it = arrClients.begin(); it != arrClients.end(); ++it){
		const std::string &sid = it->first;
		sidnum++;
		sidlist.append(sid);
	}
	if(sidnum){
		sendToRouter(ROUTER_MSG_CONN, sidlist.size(), sidlist.c_str(), 0, NULL);
	}
	
	//sync channel
	for(ChannelList::const_iterator it = arrChannels.begin(); it != arrChannels.end(); ++it){
		const std::string &cname = it->first;
		sendToRouter(ROUTER_MSG_CH_SUB, cname.size(), cname.c_str(), 0, NULL);
	}
}

void Worker::doKick(RouterMsg *pMsg)
{
	int pos = 0;
	while(pos + SID_LENGTH <= pMsg->slen)
	{
		std::string sid(pMsg->data + pos, SID_LENGTH);
		LOG("kick user, sid=%s", sid.c_str());
		ClientList::const_iterator it = arrClients.find(sid);
		if(it != arrClients.end()){
			Connection *pClient = it->second;
			pClient->close();
		}
		
		pos+= SID_LENGTH;
	}
}

void Worker::doSendMsg(RouterMsg *pMsg)
{
	int pos = 0;
	while(pos + SID_LENGTH <= pMsg->slen)
	{
		std::string sid(pMsg->data + pos, SID_LENGTH);
		ClientList::const_iterator it = arrClients.find(sid);
		if(it != arrClients.end()){
			Connection *pClient = it->second;
			LOG("send msg, sid=%s", sid.c_str());
			sendToClient(pClient, pMsg->data + pMsg->slen, pMsg->len);
		}else{
			LOG("not found client, sid=%s", sid.c_str());
		}
		
		pos+= SID_LENGTH;
	}
}

void Worker::doSendAllMsg(RouterMsg *pMsg)
{
	ClientList::const_iterator it;
	int n = 0;
	for(it = arrClients.begin(); it != arrClients.end(); ++it){
		Connection *pClient = it->second;
		sendToClient(pClient, pMsg->data, pMsg->len);
		n++;
	}
	
	LOG("sendall msg n=%d", n);
}

void Worker::doChannelAdd(RouterMsg *pMsg)
{
	//频道名称在data域
	int pos = 0;
	std::string chname(pMsg->data + pMsg->slen, pMsg->len);
	ConnectionSet &clients = arrChannels[chname];
	while(pos + SID_LENGTH <= pMsg->slen)
	{
		std::string sid(pMsg->data + pos, SID_LENGTH);
		ClientList::const_iterator it = arrClients.find(sid);
		if(it != arrClients.end()){
			Connection *pClient = it->second;
			ClientData *pClientData = (ClientData*)pClient->getData();
			pClientData->channels[chname] = 1;
			
			clients[pClient] = 1;
			if(clients.size() == 1){
				sendToRouter(ROUTER_MSG_CH_SUB, chname.size(), chname.c_str(), 0, NULL);
			}
			
			LOG("join channel name=%s, sid=%s", chname.c_str(), sid.c_str());
		}
		
		pos+= SID_LENGTH;
	}
}

void Worker::doChannelDel(RouterMsg *pMsg)
{
	//频道名称在data域
	int pos = 0;
	std::string chname(pMsg->data + pMsg->slen, pMsg->len);
	ConnectionSet &clients = arrChannels[chname];
	while(pos + SID_LENGTH <= pMsg->slen)
	{
		std::string sid(pMsg->data + pos, SID_LENGTH);
		ClientList::const_iterator it = arrClients.find(sid);
		if(it != arrClients.end()){
			Connection *pClient = it->second;
			ClientData *pClientData = (ClientData*)pClient->getData();
			pClientData->channels.erase(chname);
			
			clients.erase(pClient);
			if(!clients.size()){
				sendToRouter(ROUTER_MSG_CH_UNSUB, chname.size(), chname.c_str(), 0, NULL);
			}
			
			LOG("leave channel name=%s, sid=%s", chname.c_str(), sid.c_str());
		}
		
		pos+= SID_LENGTH;
	}
}

void Worker::doChannelPub(RouterMsg *pMsg)
{
	//频道名称在session域
	std::string chname(pMsg->data, pMsg->slen);
	ChannelList::const_iterator it = arrChannels.find(chname);
	if(it == arrChannels.end()){
		return ;
	}
	
	const ConnectionSet &clients = it->second;
	ConnectionSet::const_iterator it2;
	for(it2 = clients.begin(); it2 != clients.end(); ++it2){
		Connection *pClient = it2->first;
		ClientData *pClientData = (ClientData*)pClient->getData();
		sendToClient(pClient, pMsg->data + pMsg->slen, pMsg->len);
		
		LOG("send channel msg to: %s", pClientData->session.getId());
	}
}

//添加空闲连接
void Worker::addIdleNode(Connection *conn)
{
	Config *pCfg = Config::getInstance();
	IdleNode *node = (IdleNode*)zmalloc(sizeof(IdleNode));
	node->conn = conn;
	node->expire = (uint32_t)time(NULL) + pCfg->nIdleTime;
	
	if(pIdleHead){
		node->prev = pIdleTail;
		node->next = NULL;
		pIdleTail->next = node;
		pIdleTail = node;
	}else{
		node->next = node->prev = NULL;
		pIdleHead = pIdleTail = node;
	}
	
	ClientData *pData = (ClientData*)conn->getData();
	pData->idle = node;
}

//删除空闲连接
void Worker::delIdleNode(Connection *conn)
{
	ClientData *pData = (ClientData*)conn->getData();
	IdleNode *node = pData->idle;
	
	if(node->prev){
		node->prev->next = node->next;
	}else{
		pIdleHead = node->next;
	}
	
	if(node->next){
		node->next->prev = node->prev;
	}else{
		pIdleTail = node->prev;
	}
	
	zfree(node);
}

//重置空闲连接
void Worker::resetIdleNode(Connection *conn)
{
	ClientData *pData = (ClientData*)conn->getData();
	IdleNode *node = pData->idle;
	
	//先移除
	if(node->prev){
		node->prev->next = node->next;
	}else{
		pIdleHead = node->next;
	}
	
	if(node->next){
		node->next->prev = node->prev;
	}else{
		pIdleTail = node->prev;
	}
	
	//再重置
	Config *pCfg = Config::getInstance();
	node->expire = (uint32_t)time(NULL) + pCfg->nIdleTime;
	
	//后插入
	if(pIdleHead){
		node->prev = pIdleTail;
		node->next = NULL;
		pIdleTail->next = node;
		pIdleTail = node;
	}else{
		node->next = node->prev = NULL;
		pIdleHead = pIdleTail = node;
	}
}

void Worker::loopBefore(void *data)
{
	Config *pCfg = Config::getInstance();
	
	//accept mutex check
	if(pMaster->bUseAcceptMutex)
	{
		if(bHeldAcceptLock && 
			UnLockAcceptMutex(&pMaster->pGlobals->lock, nPid))
		{
			//LOG("unlock accept mutex");
			bHeldAcceptLock = false;
			pServer->stop();
		}else if(!bHeldAcceptLock && 
			pMaster->pGlobals->workerClients[nId] <= static_cast<int>(pMaster->pGlobals->clients * 1.125 / pCfg->nWorkers) &&
			LockAcceptMutex(&pMaster->pGlobals->lock, nPid))
		{
			//LOG("lock accept mutex");
			bHeldAcceptLock = true;
			pServer->start();
		}
	}
	
	//idle check
	if(pCfg->nIdleTime > 0){
		uint32_t now = time(NULL);
		IdleNode *node = pIdleHead;
		while(node){
			IdleNode *next = node->next;
			if(now >= node->expire){
				LOG_INFO("connection close by idle");
				node->conn->close();
			}else{
				break;
			}
			
			node = next;
		}
	}
}

void Worker::initRouter()
{
	//connect router
	Config *pConfig = Config::getInstance();
	pRouter = new Connection(pEventLoop);
	pRouter->setTimeout(3000);
	pRouter->setConnectHandler(EV_CB(this, Worker::onRouterConnect));
	pRouter->setMessageHandler(EV_CB(this, Worker::onRouterMessage));
	pRouter->setCloseHandler(EV_CB(this, Worker::onRouterClose));
	pRouter->connectTcp(pConfig->sRouterHost.c_str(), pConfig->nRouterPort);
	LOG_INFO("connect router server %s:%d", 
		pConfig->sRouterHost.c_str(), 
		pConfig->nRouterPort);
}

void Worker::proc()
{
	Config *cfg = Config::getInstance();
	
	//还原信号默认处理方式
	signal(SIGTERM, NULL);
	signal(SIGINT, NULL);
	signal(SIGUSR1, NULL);

	//set process title
	utils::setProcTitle("fooking gateway worker");
	
	//create loop
	pEventLoop = new EventLoop();
	pEventLoop->setMaxWaitTime(500);
	pEventLoop->setLoopBefore(EV_CB(this, Worker::loopBefore), NULL);
	
	//pipe event
	pPipe = new Connection(pEventLoop, arrPipes[1]);
	pPipe->setMessageHandler(EV_CB(this, Worker::onPipeMessage));
	
	//listen server
	pServer = new Server(pEventLoop, pMaster->pServer->getSocket().getFd());
	pServer->setConnectionHandler(EV_IO_CB(this, Worker::onConnection));
	if(!pMaster->bUseAcceptMutex){
		pServer->start();
	}
	
	//init router
	initRouter();
	
	//init backend
	pBackend = new Backend(pEventLoop);
	pBackend->setHandler(EV_CB(this, Worker::onBackendHandler));
	for(FastcgiParams::const_iterator it = cfg->arFastcgiParams.begin(); 
		it != cfg->arFastcgiParams.end(); ++it)
	{
		const std::string &k = it->first;
		const std::string &v = it->second;
		pBackend->addParam(k.c_str(), k.size(), v.c_str(), v.size());
	}
	
	//server running
	LOG("worker started, pipefd=%d", nPipefd);
	pScript = pMaster->pScript;
	pEventLoop->run();
}