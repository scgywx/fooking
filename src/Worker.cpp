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

void Worker::createClient(Connection *client)
{
	//计数更新
	pMaster->addClient(nId);
	
	//创建session对像
	Config *cc = Config::getInstance();
	Session sess(pMaster->pGlobals->count);
	LOG("new client, fd=%d, host=%s, port=%d, sid=%s", client->fd(), client->host(), client->port(), sess.getId());
	
	//创建ctx
	ClientContext *ctx = new ClientContext();
	char szPort[8];
	int nPort = sprintf(szPort, "%d", client->port());
	ctx->session = sess;
	ctx->nrequest = 0;
	pBackend->makeParam(ctx->params, "REMOTE_ADDR", sizeof("REMOTE_ADDR") - 1, client->host(), strlen(client->host()));
	pBackend->makeParam(ctx->params, "REMOTE_PORT", sizeof("REMOTE_PORT") - 1, szPort, nPort);
	pBackend->makeParam(ctx->params, "SESSIONID", sizeof("SESSIONID") - 1, sess.getId(), SID_LENGTH, true);
	
	//创建连接对像
	client->setContext(ctx);
	client->setMessageHandler(EV_CB(this, Worker::onMessage));
	client->setCloseHandler(EV_CB(this, Worker::onClose));
	//client->setWriteCompleteHandler(EL_CB(this, Worker::onWriteComplete));
	
	//客户端列表更新
	arrClients[sess.getId()] = client;
	
	//Router通知
	sendToRouter(ROUTER_MSG_CONN, SID_LENGTH, sess.getId(), 0, NULL);
	
	//backend通知
	if(cc->bEventConnect){
		Buffer *params = new Buffer(ctx->params);
		pBackend->makeParam(*params, "EVENT", sizeof("EVENT"), "1", sizeof("1") - 1, true);
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
	ClientContext *pCtx = static_cast<ClientContext*>(client->getContext());
	std::string sid = pCtx->session.getId();
	LOG("close client, fd=%d, sid=%s", client->getSocket().getFd(), sid.c_str());
	
	//router通知
	sendToRouter(ROUTER_MSG_CLOSE, SID_LENGTH, pCtx->session.getId(), 0, NULL);
	
	//后端通知
	Config *cc = Config::getInstance();
	if(cc->bEventClose){
		Buffer *params = new Buffer(pCtx->params);
		pBackend->makeParam(*params, "EVENT", sizeof("EVENT") - 1, "2", sizeof("2") - 1, true);
		if(!pBackend->post(NULL, NULL, params)){
			delete params;
		}
	}
	
	//释放后端请求
	for(RequestList::const_iterator it = pCtx->requests.begin(); it != pCtx->requests.end(); ++it){
		pBackend->abort(it->first);
	}
	
	//取消频道订阅
	for(ChannelSet::const_iterator it2 = pCtx->channels.begin(); it2 != pCtx->channels.end(); ++it2){
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
	
	delete pCtx;
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
	ClientContext *pCtx = static_cast<ClientContext*>(conn->getContext());
	Buffer resp;
	int ret = pScript->procWrite(conn, pCtx->nrequest, msg, &resp);
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

void Worker::onConnection(Connection *conn)
{
	//check maxclients
	Config *pConfig = Config::getInstance();
	if(pConfig->nMaxClients && pMaster->pGlobals->clients >= pConfig->nMaxClients){
		LOG("Connection is full");
		conn->close();
		return ;
	}
	
	//free accept lock
	if(pMaster->bUseAcceptMutex && 
		UnLockAcceptMutex(&pMaster->pGlobals->lock, nPid))
	{
		//LOG("unlock accept mutex");
		bHeldAcceptLock = false;
		pServer->stop();
	}
	
	//create client
	createClient(conn);
}

void Worker::onClose(Connection *conn)
{
	closeClient(conn);
}

void Worker::onMessage(Connection *client)
{
	Buffer *pBuffer = client->getBuffer();
	Config *pConfig = Config::getInstance();
	ClientContext *pCtx = static_cast<ClientContext*>(client->getContext());
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
			int ret = pScript->procRead(client, pCtx->nrequest, pBuffer, msg);
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
				delete msg;
				return ;
			}
			
			//check body
			if(pBuffer->size() - hdrSize < msgSize){
				LOG("package length not enough, msgSize=%u, buffSize=%u", msgSize, pBuffer->size());
				delete msg;
				return ;
			}
			
			msg->append(pBuffer->data() + hdrSize, msgSize);
			
			pBuffer->seek(hdrSize + msgSize);
		}
	
		pCtx->nrequest++;
		LOG("process message, fd=%d, reqid=%d, proc=%d, buffer size=%d, msg len=%d", 
			client->getSocket().getFd(), 
			pCtx->nrequest,
			proc, 
			pBuffer->size(),
			msg->size());
	
		//create request
		if(!msg->empty()){
			RequestContext *ctx = pBackend->post(client, msg, &pCtx->params);
			if(ctx){
				pCtx->requests[ctx] = 1;
			}else{
				delete msg;
			}
			
			//重置idle
			if(pConfig->nIdleTime > 0){
				resetIdleNode(client);
			}
		}else{
			delete msg;
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
			ClientContext *pCtx = static_cast<ClientContext*>(client->getContext());
			pCtx->requests.erase(ctx);
			
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
			ClientContext *pCtx = (ClientContext*)pClient->getContext();
			pCtx->channels[chname] = 1;
			
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
			ClientContext *pCtx = (ClientContext*)pClient->getContext();
			pCtx->channels.erase(chname);
			
			clients.erase(pClient);
			if(clients.size() == 0){
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
		ClientContext *pCtx = (ClientContext*)pClient->getContext();
		sendToClient(pClient, pMsg->data + pMsg->slen, pMsg->len);
		
		LOG("send channel msg to: %s", pCtx->session.getId());
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
	
	ClientContext *pCtx = (ClientContext*)conn->getContext();
	pCtx->idle = node;
}

//删除空闲连接
void Worker::delIdleNode(Connection *conn)
{
	ClientContext *pCtx = (ClientContext*)conn->getContext();
	IdleNode *node = pCtx->idle;
	
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
	ClientContext *pCtx = (ClientContext*)conn->getContext();
	IdleNode *node = pCtx->idle;
	
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

	//set process title
	utils::setProcTitle("fooking gateway worker");
	
	//create loop
	pEventLoop = new EventLoop();
	pEventLoop->setMaxWaitTime(500);
	pEventLoop->setLoopBefore(EV_CB(this, Worker::loopBefore), NULL);
	
	//pipe event
	pPipe = new Connection(pEventLoop, arrPipes[1]);
	pPipe->setMessageHandler(EV_CB(this, Worker::onPipeMessage));
	pPipe->attach();
	
	//listen server
	pServer = pMaster->pServer;
	pServer->setEventLoop(pEventLoop);
	pServer->setConnectionHandler(EV_CB(this, Worker::onConnection));
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