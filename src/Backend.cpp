#include <algorithm>
#include "fooking.h"
#include "Backend.h"
#include "Log.h"
#include "Config.h"

//version
#define FCGI_VERSION_1	1

//type
#define FCGI_BEGIN_REQUEST  	1
#define FCGI_ABORT_REQUEST  	2
#define FCGI_END_REQUEST  		3
#define FCGI_PARAMS  			4
#define FCGI_STDIN  			5
#define FCGI_STDOUT  			6
#define FCGI_STDERR  			7
#define FCGI_DATA  				8
#define FCGI_GET_VALUES  		9
#define FCGI_GET_VALUES_RESULT  10
#define FCGI_UNKNOWN_TYPE  		11

#define FCGI_MAXTYPE  			11

#define FCGI_RESPONDER  		1
#define FCGI_AUTHORIZER  		2
#define FCGI_FILTER  			3

#define FCGI_REQUEST_COMPLETE  	0
#define FCGI_CANT_MPX_CONN  	1
#define FCGI_OVERLOADED  		2
#define FCGI_UNKNOWN_ROLE  		3

#define FCGI_HEADER_LEN  		8

#define BACKEND_RETRY_MAX		1

#define ALIGN(size) ((size & 7) ? (8 - (size & 7)) : 0)

#define CTX_RELEASE(_ctx) if(nKeepalive){\
	if(nIdleTop >= nKeepalive){\
		_ctx->backend->close();\
	}else{\
		arrIdleBackends[nIdleTop++] = _ctx;\
		_ctx->index = nIdleTop;\
	}\
}
	
NS_USING;

Backend::Backend(EventLoop *loop):
	pLoop(loop),
	nRoll(0),
	nIdleTop(0),
	arrIdleBackends(NULL)
{
	EV_CB_INIT(cbHandler);
	
	Config *cc = Config::getInstance();
	nServers = cc->arrBackendServer.size();
	nKeepalive = cc->nBackendKeepalive;
	nConnectTimeout = cc->nBackendConnectTimeout;
	nReadTimeout = cc->nBackendReadTimeout;
	
	//init roll
	for(int n = 0; n < nServers; ++n)
	{
		BackendOption &opt = cc->arrBackendServer[n];
		
		if(opt.weight <= 0 || (opt.type != SOCKET_TCP && opt.type != SOCKET_UNIX)){
			continue;
		}
		
		for(int i = 0; i < opt.weight; ++i){
			arrServerRolls.push_back(n);
		}
	}
	
	//shuffle
	std::random_shuffle(arrServerRolls.begin(), arrServerRolls.end());
	
	//keepalive stack
	if(nKeepalive){
		arrIdleBackends = (RequestContext**)zmalloc(sizeof(RequestContext*) * nKeepalive);
	}
}

Backend::~Backend()
{
	//TODO
}

RequestContext* Backend::post(Connection *client, Buffer *req, Buffer *params)
{
	RequestContext *ctx = NULL;
	
	if(nKeepalive && nIdleTop){
		ctx = arrIdleBackends[--nIdleTop];
		
		//request
		ctx->client = client;
		ctx->req = req;
		ctx->params = params;
		ctx->abort = 0;
		ctx->timer = 0;
		ctx->index = 0;
		request(ctx);
		
		return ctx;
	}
	
	ctx = (RequestContext*)zmalloc(sizeof(RequestContext));
	memset(ctx, 0, sizeof(RequestContext));
	ctx->client = client;
	ctx->req = req;
	ctx->params = params;
	
	if(connect(ctx)){
		return ctx;
	}
	
	delete ctx;
	
	return NULL;
}

void Backend::onConnect(Connection *conn)
{
	LOG("backend connected, conn=%p, fd=%d", conn, conn->getSocket().getFd());
	
	RequestContext *ctx = static_cast<RequestContext*>(conn->getData());
	if(ctx->timer){
		pLoop->stopTimer(ctx->timer);
		ctx->timer = 0;
	}
	
	if(ctx->abort){
		EV_INVOKE(cbHandler, ctx);
		CTX_RELEASE(ctx);
	}else{
		request(ctx);
	}
}

void Backend::onMessage(Connection *conn)
{
	LOG("backend data, conn=%p", conn);
	
	RequestContext *ctx = static_cast<RequestContext*>(conn->getData());
	if(response(ctx)){
		//invoke
		EV_INVOKE(cbHandler, ctx);
		
		//clear response
		if(ctx->rep){
			ctx->rep->clear();
		}
		
		//stop timer
		if(ctx->timer){
			pLoop->stopTimer(ctx->timer);
			ctx->timer = 0;
		}
		
		//release
		CTX_RELEASE(ctx);
	}
}

void Backend::onClose(Connection *conn)
{
	int err = conn->getError();
	RequestContext *ctx = static_cast<RequestContext*>(conn->getData());
	
	//stop timer
	if(ctx->timer){
		pLoop->stopTimer(ctx->timer);
	}
	
	if(err){
		LOG_ERR("backend error=%d, errstr=%s", err, strerror(err));
		
		//retry connect
		if(!conn->isConnected()){
			if(ctx->fails == NULL){
				size_t sz = nServers * sizeof(uint8_t);
				ctx->fails = (uint8_t*)zmalloc(sz);
				memset(ctx->fails, 0, sz);
			}
			
			ctx->fails[ctx->serverid]++;
			
			if(connect(ctx)){
				delete conn;
				return ;
			}else{
				abort(ctx);
				EV_INVOKE(cbHandler, ctx);
			}
		}
	}else{
		LOG("backend close, conn=%p", conn);
	}
	
	//把栈顶的元素移到当前位置来
	if(ctx->index){
		--nIdleTop;
		if(nIdleTop = ctx->index){
			arrIdleBackends[ctx->index - 1] = arrIdleBackends[nIdleTop];
		}
	}
	
	zfree(ctx->fails);
	delete conn;
	delete ctx->rep;
	zfree(ctx);
}

void Backend::onWriteComplete(Connection *conn)
{
	LOG("backend write completed");
}

void Backend::onTimeout(TimerId id, Connection *conn)
{
	LOG("backend read timeout");
	
	RequestContext *ctx = static_cast<RequestContext*>(conn->getData());
	
	abort(ctx);
	EV_INVOKE(cbHandler, ctx);
	conn->close();
}

bool Backend::connect(RequestContext *ctx)
{
	Config *cc = Config::getInstance();
	int old = nRoll;
	
	do{
		int serverid = arrServerRolls[nRoll];
		if(!ctx->fails || ctx->fails[serverid] < BACKEND_RETRY_MAX){
			//创建连接
			BackendOption &opt = cc->arrBackendServer[serverid];
			Connection *conn = new Connection(pLoop);
			conn->setConnectHandler(EV_CB(this, Backend::onConnect));
			conn->setMessageHandler(EV_CB(this, Backend::onMessage));
			conn->setCloseHandler(EV_CB(this, Backend::onClose));
			if(nConnectTimeout){
				ctx->timer = pLoop->setTimer(nConnectTimeout * 1000, EV_TIMER_CB(this, Backend::onTimeout), conn);
			}
			
			if(opt.type == SOCKET_TCP){
				LOG("backend connect to %s:%d", opt.host.c_str(), opt.port);
				conn->connectTcp(opt.host.c_str(), opt.port);
			}else{
				LOG("backend connect to unix:%s", opt.host.c_str());
				conn->connectUnix(opt.host.c_str());
			}
			
			//binding context
			ctx->serverid = serverid;
			ctx->backend = conn;
			conn->setData(ctx);
			
			return true;
		}
		
		if(++nRoll >= arrServerRolls.size()){
			nRoll = 0;
		}
	}while(old != nRoll);
	
	LOG_ERR("Can't found available backend server, Please check BACKEND_SERVER");
	
	return false;
}

bool Backend::request(RequestContext *ctx)
{
	static const char *paddings = "\0\0\0\0\0\0\0\0";
	Connection *conn = ctx->backend;
	Buffer params;
	
	//header
	FastCGIHeader header = makeHeader(FCGI_BEGIN_REQUEST, 1, sizeof(FastCGIBeginRequest), 0);
	conn->send((const char*)&header, sizeof(FastCGIHeader));
	
	//begin request
	FastCGIBeginRequest begin = makeBeginRequest(FCGI_RESPONDER, nKeepalive ? 1 : 0);
	conn->send((const char*)&begin, sizeof(FastCGIBeginRequest));
	
	//content length
	if(ctx->req && !ctx->req->empty()){
		char clbuf[32];
		int clsize = sprintf(clbuf, "%u", ctx->req->size());
		makeParam(params, "CONTENT_LENGTH", sizeof("CONTENT_LENGTH") - 1, clbuf, clsize);
	}
	
	//params begin
	int pmsz = bParams.size() + (ctx->params ? ctx->params->size() : 0) + params.size();
	int pdsz = ALIGN(pmsz);
	FastCGIHeader hdrParam = makeHeader(FCGI_PARAMS, 1, pmsz, pdsz);
	conn->send((const char*)&hdrParam, sizeof(FastCGIHeader));
	
	//shared params
	if(!bParams.empty()){
		conn->send(bParams);
	}
	
	//extension params
	if(ctx->params && !ctx->params->empty()){
		conn->send(*ctx->params);
	}
		
	//local params
	if(!params.empty()){
		conn->send(params);
	}
	
	//padding
	if(pdsz){
		conn->send(paddings, pdsz);
	}
	
	//params end
	FastCGIHeader endParam = makeHeader(FCGI_PARAMS, 1, 0, 0);
	conn->send((const char*)&endParam, sizeof(FastCGIHeader));
	
	//stdin begin
	if(ctx->req && !ctx->req->empty()){
		int rpdsz = ALIGN(ctx->req->size());
		FastCGIHeader inh = makeHeader(FCGI_STDIN, 1, ctx->req->size(), rpdsz);
		conn->send((const char*)&inh, sizeof(FastCGIHeader));
		conn->send(ctx->req->data(), ctx->req->size());
		if(rpdsz){
			conn->send(paddings, rpdsz);
		}
	}
	
	//stdin end
	FastCGIHeader endh = makeHeader(FCGI_STDIN, 1, 0, 0);
	conn->send((const char*)&endh, sizeof(FastCGIHeader));
	
	//set timer
	if(nReadTimeout){
		ctx->timer = pLoop->setTimer(nReadTimeout * 1000, EV_TIMER_CB(this, Backend::onTimeout), conn);
	}
	
	return true;
}

bool Backend::response(RequestContext *ctx)
{
	Connection *conn = ctx->backend;
	Buffer *pBuffer = conn->getBuffer();
	const char *inbuf = pBuffer->data();
	size_t inlen = pBuffer->size();
	size_t offset = 0;
	bool finish = false;
	
	while(offset < inlen)
	{
		//check head
		if(offset + sizeof(FastCGIHeader) > inlen){
			break;
		}
		
		//parse header
		FastCGIHeader *header = (FastCGIHeader*)(inbuf + offset);
		size_t requestId = (header->requestIdB1 << 8) | header->requestIdB0;
		size_t contentLength = (header->contentLengthB1 << 8) | header->contentLengthB0;
		int lineSize = sizeof(FastCGIHeader) + contentLength + header->paddingLength;
		
		//check size
		if(offset + lineSize  > inlen){
			break;
		}
		
		//data
		const char *dataptr = (const char *)header + sizeof(FastCGIHeader);
		
		//log
		LOG("version=%d, type=%d, requestid=%d, contentLength=%d, paddingLength=%d, reserved=%d, data=%s",
			header->version,
			header->type,
			requestId,
			contentLength,
			header->paddingLength,
			header->reserved,
			dataptr);
		
		//process response
		switch(header->type)
		{
			case FCGI_STDERR:
			{
				LOG_ERR("backend error!!!, %s", dataptr);
				break;
			}
			case FCGI_STDOUT:
			{
				FastCGIBody body = parse(dataptr, contentLength);
				if(body.length){
					if(!ctx->rep){
						ctx->rep = new Buffer();
					}
					
					if(body.offset >= 0){
						ctx->rep->append(body.ptr + body.offset, body.length);
					}else{
						ctx->rep->append(body.ptr + body.total + body.offset, body.length);
					}
				}
				break;
			}
			case FCGI_END_REQUEST:
			{
				//检测请求
				FastCGIEndRequest *pEndReq = (FastCGIEndRequest*)dataptr;
				int exitStatus = (pEndReq->appStatusB3 << 24)
							   + (pEndReq->appStatusB2 << 16)
							   + (pEndReq->appStatusB1 <<  8)
							   + (pEndReq->appStatusB0      );
				LOG("FastCGI End Request, protocolStatus=%d, exitStatus=%d",
					pEndReq->protocolStatus,
					exitStatus);
				
				finish = true;
				break;
			}
		}
		
		//offset
		offset+= lineSize;
	}
	
	pBuffer->seek(offset);
	
	return finish;
}

void Backend::addParam(const char *name, int namelen, const char *value, int vallen)
{
	makeParam(bParams, name, namelen, value, vallen);
}

FastCGIHeader Backend::makeHeader(int type, int requestId, int contentLength, int paddingLength)
{
	FastCGIHeader header;
	header.version 			= FCGI_VERSION_1;
	header.type             = (unsigned char) type;
	header.requestIdB1      = (unsigned char) ((requestId     >> 8) & 0xff);
	header.requestIdB0      = (unsigned char) ((requestId         ) & 0xff);
	header.contentLengthB1  = (unsigned char) ((contentLength >> 8) & 0xff);
	header.contentLengthB0  = (unsigned char) ((contentLength     ) & 0xff);
	header.paddingLength    = (unsigned char) paddingLength;
	header.reserved         = 0;
	return header;
}

FastCGIBeginRequest Backend::makeBeginRequest(int role, int flag)
{
	FastCGIBeginRequest body;
	body.roleB1 = (unsigned char) ((role >>  8) & 0xff);
	body.roleB0 = (unsigned char) (role         & 0xff);
	body.flags  = (unsigned char) ((flag) ? 1 : 0);
	memset(body.reserved, 0, sizeof(body.reserved));
	return body;
}

int	Backend::makeParam(Buffer &buf, const char *name, int namelen, const char *value, int vallen)
{
	char npair[4];
	char vpair[4];
	int nnpair = 1;
	int nvpair = 1;

	if (namelen < 128) {
		npair[0] = namelen;
	} else {
		npair[0] = (namelen >> 24) | 0x80;
		npair[1] = (namelen >> 16) & 0xFF;
		npair[2] = (namelen >> 8) & 0xFF;
		npair[3] = (namelen & 0xFF);
		nnpair = 4;
	}

	if (vallen < 128) {
		vpair[0] = vallen;
	} else {
		vpair[0] = (vallen >> 24) | 0x80;
		vpair[1] = (vallen >> 16) & 0xFF;
		vpair[2] = (vallen >> 8) & 0xFF;
		vpair[3] = (vallen & 0xFF);
		nvpair = 4;
	}

	buf.append(npair, nnpair);
	buf.append(vpair, nvpair);
	buf.append(name, namelen);
	if(vallen){
		buf.append(value, vallen);
	}

	return nnpair + nvpair + namelen + vallen;
}

FastCGIBody	Backend::parse(const char *ptr, int len)
{
	static unsigned char lowcase[] =
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0-\0\0" "0123456789\0\0\0\0\0\0"
        "\0abcdefghijklmnopqrstuvwxyz\0\0\0\0\0"
        "\0abcdefghijklmnopqrstuvwxyz\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";

	FastCGIBody body;
	char name[128];
	char value[128];
	int namelen = 0;
	int vallen = 0;
	int state = kStart;
	unsigned char ch, c;
	bool lineDone;

	memset(&body, 0, sizeof(FastCGIBody));

	for(int i = 0; i < len; ++i)
	{
		lineDone = false;
		ch = ptr[i];

		if(ch == '\r'){
			continue;
		}

		switch(state){
		case kStart:
			switch(ch){
			case '\n':
				state = kBody;
				break;
			default:
				state = kName;
				namelen = vallen = 0;
				c = lowcase[ch];
				if(c){
					name[0] = c;
					namelen = 1;
				}
				break;
			}
			break;
		case kName:
			c = lowcase[ch];
			if(c){
				name[namelen++] = c;
				break;
			}

			switch(ch){
			case '\n':
				lineDone = true;
				break;
			case ':':
				state = kValueReady;
				break;
			}
			break;
		case kValueReady:
			switch(ch){
			case '\n':
				lineDone = true;
				break;
			case ' ':
				break;
			default:
				state = kValue;
				value[0] = ch;
				vallen = 1;
				break;
			}
			break;
		case kValue:
			switch(ch){
			case '\n':
				lineDone = true;
				break;
			default:
				value[vallen++] = ch;
				break;
			}
			break;
		case kBody:
			body.ptr = ptr + i;
			body.total = len - i;
			break;
		}

		if(lineDone){
			state = kStart;
			//check Content-Length & Content-Offset
			if(namelen == 14 && vallen){
				name[namelen] = '\0';
				value[vallen] = '\0';
				if(strncmp(name, "content-length", 14) == 0){
					int v = atoi(value);
					body.length = v;
				}else if(strncmp(name, "content-offset", 14) == 0){
					int v = atoi(value);
					body.offset = v;
				}
			}
		}

		if(body.ptr){
			break;
		}
	}
	
	LOG("total=%d, length=%d, offset=%d\n", body.total, body.length, body.offset);
	
	//check
	if(body.offset < 0 && body.length + body.offset > 0){
		body.length = 0;
	}else if(body.offset > 0 && body.offset + body.length > body.total){
		body.length = 0;
	}
	
	return body;
}
