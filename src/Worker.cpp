#include <iostream>
#include <signal.h>
#include "Worker.h"
#include "Socket.h"
#include "Buffer.h"
#include "Log.h"
#include "Master.h"
#include "Session.h"
#include "Router.h"
#include "Utils.h"
#include "Atomic.h"

NS_USING;

Worker::Worker(Master *master, int id):
	nId(id),
	pMaster(master),
	pEventLoop(NULL),
	nRouterReconnect(0),
	nRouterDelay(0)
{
	pProtocol = new ProtocolFastcgi();
}

Worker::~Worker()
{
	delete pEventLoop;
	delete pProtocol;
}

void Worker::createClient(int fd, const char *ip, int port)
{
	//创建session对像
	Session sess(nPid, fd);
	LOG("new client, fd=%d, ip=%s, port=%d, sid=%s", fd, ip, port, sess.getId());
	
	//辅助数据
	ClientData *pData = new ClientData();
	char szPort[8];
	int nPort = sprintf(szPort, "%d", port);
	pData->session = sess;
	pProtocol->makeParam(pData->params, "REMOTE_ADDR", sizeof("REMOTE_ADDR") - 1, ip, strlen(ip));
	pProtocol->makeParam(pData->params, "REMOTE_PORT", sizeof("REMOTE_PORT") - 1, szPort, nPort);
	pProtocol->makeParam(pData->params, "SESSIONID", sizeof("SESSIONID") - 1, sess.getId(), SID_LENGTH);
	
	//创建连接对像
	Connection *client = new Connection(pEventLoop, fd);
	client->setIPAndPort(ip, port);
	client->setData(pData);
	client->setMessageHandler(EV_CB(this, Worker::onMessage));
	client->setCloseHandler(EV_CB(this, Worker::onClose));
	//client->setWriteCompleteHandler(EL_CB(this, Worker::onWriteComplete));
	
	arrClients[sess.getId()] = client;
	
	//计数更新
	pMaster->pGlobals->workerClients[nId]++;
	atomic_fetch_add(&pMaster->pGlobals->clients, 1);
	
	//Router通知
	sendToRouter(ROUTER_MSG_CONN, SID_LENGTH, sess.getId(), 0, NULL);
	
	//backend通知
	if(pMaster->pConfig->bEventConnect){
		Buffer *pParams = new Buffer(pData->params);
		pProtocol->makeParam(*pParams, "EVENT", sizeof("EVENT") - 1, "1", 1);
		createBackendRequest(client, NULL, pParams, true);
	}
}

void Worker::closeClient(Connection *client)
{
	ClientData *pData = (ClientData*)client->getData();
	std::string sid = pData->session.getId();
	LOG("close client, fd=%d, sid=%s", client->getSocket().getFd(), sid.c_str());
	
	//router通知
	sendToRouter(ROUTER_MSG_CLOSE, SID_LENGTH, pData->session.getId(), 0, NULL);
	
	//后端通知
	if(pMaster->pConfig->bEventClose){
		Buffer *pParams = new Buffer(pData->params);
		pProtocol->makeParam(*pParams, "EVENT", sizeof("EVENT") - 1, "2", 1);
		createBackendRequest(NULL, NULL, pParams, true);
	}
	
	//释放后端请求
	for(BackendList::const_iterator it = pData->backends.begin(); it != pData->backends.end(); ++it){
		Connection *backend = it->first;
		BackendRequest *request = (BackendRequest*)backend->getData();
		request->client = NULL;//大哥已死，小弟们自生自灭...
		backend->close();
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
	pMaster->pGlobals->workerClients[nId]--;
	atomic_fetch_sub(&pMaster->pGlobals->clients, 1);
	
	arrClients.erase(sid);
	
	delete pData;
	delete client;
}

void Worker::sendToRouter(uint_16 type, uint_16 slen, const char *sessptr, int len, const char *dataptr)
{
	if(pRouter){
		RouterMsg msg;
		writeNetInt16((char*)&msg.type, type);
		writeNetInt16((char*)&msg.slen, slen);
		writeNetInt32((char*)&msg.len, len);
		pRouter->send((char*)&msg, ROUTER_HEAD_SIZE);
		if(slen){
			pRouter->send(sessptr, slen);
		}
		if(len){
			pRouter->send(dataptr, len);
		}
	}
}

#define SEND_TO_CLIENT_RAW(_dat, _len) \
	char hdr[4];\
	writeNetInt32(hdr, _len);\
	conn->send(hdr, 4);\
	if(_len) conn->send(_dat, _len)

void Worker::sendToClient(Connection *conn, const char *data, int len)
{
	if(pMaster->pScript){
		Buffer buff(data, len);
		Buffer response;
		int ret = pMaster->pScript->procOutput(&buff, &response);
		if(ret > 0){
			if(response.size()){
				conn->send(response.data(), response.size());
			}
		}else if(ret == 0){
			if(buff.size()){
				SEND_TO_CLIENT_RAW(buff.data(), buff.size());
			}
		}else{
			//Nothing TODO
			//协议处理错误
		}
	}else{
		SEND_TO_CLIENT_RAW(data, len);
	}
}

void Worker::onChannel(int fd, int ev, void *data)
{
	LOG("on channel");
	ChannelMsg ch;
	int n = recv(&ch);
	if(n){
		pEventLoop->removeEventListener(fd, EV_IO_ALL);
		close(fd);
		LOG_ERR("[worker]broken pipe");
		return ;
	}
	
	switch(ch.type){
		case CH_PIPE:
			LOG("channel msg pipefd");
			break;
		case CH_RELOAD:
			LOG("channel msg reload");
			break;
		case CH_EXIT:
			LOG("channel msg exit");
			break;
	}
}

void Worker::onConnection(int fd, int ev, void *data)
{
	//check maxclients
	if(pMaster->pGlobals->clients >= pMaster->pConfig->nMaxClients){
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
	if(pMaster->bAcceptLock && 
		UnLockAcceptMutex(&pMaster->pGlobals->lock, nPid))
	{
		LOG("unlock accept mutex");
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
	ClientData *pData = (ClientData*)client->getData();
	
	LOG("client message, fd=%d, len=%d", client->getSocket().getFd(), pBuffer->size());
	
	while(pBuffer->size())
	{
		bool proc = false;
		Buffer *req = new Buffer();
		
		//自定义协议处理
		if(pMaster->pScript){
			int ret = pMaster->pScript->procInput(pBuffer, req);
			if(ret < 0){
				delete req;
				return ;
			}else if(ret > 0){
				proc = true;
			}
		}
		
		if(!proc){
			size_t hdrSize = 4;
			if(pBuffer->size() < hdrSize){
				delete req;
				return ;
			}
			
			size_t msgSize = readNetInt32(pBuffer->data());
			if(pBuffer->size() < hdrSize + msgSize){
				delete req;
				return ;
			}
			
			req->append(pBuffer->data() + hdrSize, msgSize);
			
			pBuffer->seek(hdrSize + msgSize);
		}
	
		LOG("process message, fd=%d, size=%d, proc=%d, len=%d", client->getSocket().getFd(), req->size(), proc, pBuffer->size());
	
		//create request
		Connection *backend = createBackendRequest(client, req, &pData->params);
		if(backend){
			pData->backends[backend] = 1;
		}else{
			LOG_ERR("not found backend server");
		}
	}
}

Connection* Worker::createBackendRequest(Connection *client, Buffer *request, Buffer *params, bool dParam)
{
	Connection *backend = NULL;
	int r = Utils::randInt(1, pMaster->pConfig->nMaxBackendWeights);
	int servers = pMaster->pConfig->arrBackendServer.size();
	for(int i = 0; i < servers; ++i){
		BackendOption &opt = pMaster->pConfig->arrBackendServer[i];
		r-= opt.weight;
		if(r <= 0 && (opt.type == SOCKET_TCP || opt.type == SOCKET_UNIX))
		{
			//创建连接
			backend = new Connection(pEventLoop);
			backend->setConnectHandler(EV_CB(this, Worker::onBackendConnect));
			backend->setMessageHandler(EV_CB(this, Worker::onBackendMessage));
			backend->setCloseHandler(EV_CB(this, Worker::onBackendClose));
			if(opt.type == SOCKET_TCP){
				LOG("backend connect to %s:%d", opt.host.c_str(), opt.port);
				backend->connectTcp(opt.host.c_str(), opt.port);
			}else{
				LOG("backend connect to unix:%s", opt.host.c_str());
				backend->connectUnix(opt.host.c_str());
			}
			
			//创建请求
			BackendRequest *pRequest = new BackendRequest();
			pRequest->client = client;
			pRequest->request = request;
			pRequest->dParam = dParam;
			pRequest->params = params;
			pRequest->connected = false;
			
			//绑定请求
			backend->setData(pRequest);
			
			//加入到超时列表
			if(pMaster->pConfig->nBackendTimeout){
				arrExpireBackends[backend] = pMaster->pConfig->nBackendTimeout;
			}
			
			break;
		}
	}
	
	return backend;
}

void Worker::onBackendConnect(Connection *backend)
{
	LOG("backend connected, conn=%p", backend);
	BackendRequest *pRequest = (BackendRequest*)backend->getData();
	Buffer bkRequest;
	const char *req = NULL;
	size_t len = 0;
	
	pRequest->connected = true;
	if(pRequest->request){
		req = pRequest->request->data();
		len = pRequest->request->size();
	}
	
	pProtocol->pack(req, len, bkRequest, pRequest->params);
	backend->send(bkRequest);
	
	arrExpireBackends.erase(backend);
}

void Worker::onBackendMessage(Connection *backend)
{
	LOG("backend data");
	BackendRequest *pRequest = (BackendRequest*)backend->getData();
	Buffer resp;
	Buffer *pBuffer = backend->getBuffer();
	int n = pProtocol->unpack(pBuffer->data(), pBuffer->size(), resp);
	if(n > 0){
		pBuffer->seek(n);
		if(pRequest->client && resp.size()){
			sendToClient(pRequest->client, resp.data(), resp.size());
		}
	}
}

void Worker::onBackendClose(Connection *backend)
{
	LOG("backend close, conn=%p", backend);
	BackendRequest *pRequest = (BackendRequest*)backend->getData();
	Connection *pClient = pRequest->client;
	if(pClient){
		ClientData *pClientData = (ClientData*)pClient->getData();
		pClientData->backends.erase(backend);
	}
	
	if(!pRequest->connected){
		arrExpireBackends.erase(backend);
	}
	
	if(pRequest->dParam){
		delete pRequest->params;
	}
	
	if(pRequest->request){
		delete pRequest->request;
	}
	
	delete pRequest;
	delete backend;
}

void Worker::onBackendWriteComplete(Connection *backend)
{
	//LOG("backend write completed");
}

void Worker::onRouterMessage(Connection *router)
{
	Buffer *pBuffer = router->getBuffer();
	while(pBuffer->size())
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
	LOG_ERR("router close");
	delete pRouter;
	pRouter = NULL;
	
	//下一次连接的时间
	nRouterDelay = nRouterReconnect * 3;
	nRouterReconnect++;
}

void Worker::onRouterConnect(Connection *router)
{
	LOG_INFO("router connected");
	
	nRouterReconnect = 0;
	nRouterDelay = 0;
	
	//AUTH
	char id[10];
	writeNetInt32(id, pMaster->pConfig->nServerId);
	sendToRouter(ROUTER_MSG_AUTH, 0, NULL, sizeof(int), id);
	
	//sync session
	int sidnum = 0;
	std::string sidlist;
	for(ClientList::const_iterator it = arrClients.begin(); it != arrClients.end(); ++it){
		const std::string &sid = it->first;
		sidnum++;
		sidlist.append(sid);
	}
	if(sidnum){
		sendToRouter(ROUTER_MSG_CONN, sidnum * SID_LENGTH, sidlist.c_str(), 0, NULL);
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
		
		Connection *pClient = arrClients[sid];
		if(pClient){
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
		Connection *pClient = arrClients[sid];
		if(pClient){
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
		Connection *pClient = arrClients[sid];
		if(pClient){
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
		Connection *pClient = arrClients[sid];
		if(pClient){
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

void Worker::loopBefore(void *data)
{
	if(pMaster->bAcceptLock && 
		pMaster->pGlobals->workerClients[nId] < nPerWorkerAcceptMax &&
		LockAcceptMutex(&pMaster->pGlobals->lock, nPid))
	{
		LOG("lock accept mutex");
		pServer->start();
	}
}

void Worker::onTimer(long long id, void *data)
{
	//router check
	if(!pRouter && --nRouterDelay <= 0){
		initRouter();
	}
	
	//timeout check
	for(BackendList::iterator it = arrExpireBackends.begin();
		it != arrExpireBackends.end(); ++it)
	{
		if(--it->second <= 0){
			LOG("backend timeout, conn=%p", it->first);
			it->first->close();
		}
	}
}

void Worker::initRouter()
{
	//connect router
	pRouter = new Connection(pEventLoop);
	pRouter->setConnectHandler(EV_CB(this, Worker::onRouterConnect));
	pRouter->setMessageHandler(EV_CB(this, Worker::onRouterMessage));
	pRouter->setCloseHandler(EV_CB(this, Worker::onRouterClose));
	pRouter->connectTcp(pMaster->pConfig->sRouterHost.c_str(), pMaster->pConfig->nRouterPort);
	LOG_INFO("connect router server %s:%d", 
		pMaster->pConfig->sRouterHost.c_str(), pMaster->pConfig->nRouterPort);
}

void Worker::proc()
{
	//还原信号默认处理方式
	signal(SIGTERM, NULL);
	signal(SIGUSR1, NULL);

	//set process title
	pMaster->setProcTitle("fooking gateway worker");
	
	//每个worker最大允许的连接数
	nPerWorkerAcceptMax = static_cast<int>(pMaster->pConfig->nMaxClients / pMaster->pConfig->nWorkers * 1.05);
	
	//create loop
	pEventLoop = new EventLoop();
	pEventLoop->setTimer(1000, EV_TIMER_CB(this, Worker::onTimer), NULL);
	pEventLoop->addEventListener(nPipefd, EV_IO_READ, EV_IO_CB(this, Worker::onChannel), NULL);
	pEventLoop->setLoopBefore(EV_CB(this, Worker::loopBefore), NULL);
	
	//listen server
	pServer = new Server(pEventLoop, pMaster->pServer->getSocket().getFd());
	pServer->setConnectionHandler(EV_IO_CB(this, Worker::onConnection));
	if(!pMaster->bAcceptLock){
		pServer->start();
	}
	
	//init router
	initRouter();
	
	LOG("worker started, pipefd=%d", nPipefd);
	pEventLoop->run();
}