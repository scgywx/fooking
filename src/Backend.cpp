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

NS_USING;

Buffer Backend::bSharedParams;

Backend::Backend(EventLoop *loop, Connection *client, Buffer *request):
	pLoop(loop),
	pClient(client),
	pBackend(NULL),
	pRequest(request)
{
	Config *pConfig = Config::getInstance();
	int sz = sizeof(unsigned int) * pConfig->arrBackendServer.size();
	pServerFail = (unsigned int*)zmalloc(sz);
	memset(pServerFail, 0, sz);
	nWeights = pConfig->nMaxBackendWeights;
}

Backend::~Backend()
{
	shutdown();
	zfree(pServerFail);
	delete pRequest;
}

bool Backend::run()
{
	Config *pConfig = Config::getInstance();
	int r = utils::randInt(1, nWeights);
	int nserver = pConfig->arrBackendServer.size();
	int start = utils::randInt(0, nserver - 1);
	int old = start;
	
	do{
		if(pServerFail[start] < BACKEND_RETRY_MAX){
			BackendOption &opt = pConfig->arrBackendServer[start];
			r-= opt.weight;
			if(r <= 0 && (opt.type == SOCKET_TCP || opt.type == SOCKET_UNIX))
			{
				//创建连接
				Connection *backend = new Connection(pLoop);
				backend->setConnectHandler(EV_CB(this, Backend::onConnect));
				backend->setMessageHandler(EV_CB(this, Backend::onMessage));
				backend->setCloseHandler(EV_CB(this, Backend::onClose));
				if(pConfig->nBackendTimeout){
					backend->setTimeout(pConfig->nBackendTimeout * 1000);
				}
				if(opt.type == SOCKET_TCP){
					LOG("backend connect to %s:%d", opt.host.c_str(), opt.port);
					backend->connectTcp(opt.host.c_str(), opt.port);
				}else{
					LOG("backend connect to unix:%s", opt.host.c_str());
					backend->connectUnix(opt.host.c_str());
				}
				
				pBackend = backend;
				nBackendId = start;
				
				return true;
			}
		}
		
		if(++start >= nserver){
			start = 0;
		}
		
	}while(old != start);
	
	LOG_ERR("Can't found available backend server, Please check BACKEND_SERVER");
	
	return false;
}

void Backend::shutdown()
{
	if(pBackend){
		delete pBackend;
		pBackend = NULL;
	}
}

void Backend::onConnect(Connection *conn)
{
	LOG("backend connected, conn=%p, fd=%d", conn, conn->getSocket().getFd());
	
	request();
}

void Backend::onMessage(Connection *conn)
{
	LOG("backend data, conn=%p", conn);
	
	if(response()){
		EV_INVOKE(cbResponse, this);
	}
}

void Backend::onClose(Connection *conn)
{
	bool retry = false;
	int err = conn->getError();
	if(err){
		LOG_ERR("backend error=%d, errstr=%s", err, strerror(err));
		
		//retry connect
		if(!conn->isConnected()){
			retry = true;
			
			if(++pServerFail[nBackendId] >= BACKEND_RETRY_MAX){
				Config *pCfg = Config::getInstance();
				nWeights-= pCfg->arrBackendServer[nBackendId].weight;
				if(nWeights <= 0){
					retry = false;
				}
			}
		}
	}else{
		LOG("backend close, conn=%p", conn);
	}
	
	//release
	shutdown();
	
	if(retry && run()){
		return ;
	}
	
	EV_INVOKE(cbClose, this);
}

void Backend::onWriteComplete(Connection *conn)
{
	LOG("backend write completed");
}

bool Backend::request()
{
	Buffer params;
	
	//header
	FastCGIHeader header = makeHeader(FCGI_BEGIN_REQUEST, 1, sizeof(FastCGIBeginRequest), 0);
	pBackend->send((const char*)&header, sizeof(FastCGIHeader));
	
	//begin request
	FastCGIBeginRequest begin = makeBeginRequest(FCGI_RESPONDER, 0);
	pBackend->send((const char*)&begin, sizeof(FastCGIBeginRequest));
	
	//content length
	if(pRequest && !pRequest->empty()){
		char clbuf[32];
		int clsize = sprintf(clbuf, "%u", pRequest->size());
		makeParam(params, "CONTENT_LENGTH", sizeof("CONTENT_LENGTH") - 1, clbuf, clsize);
	}
	
	//params begin
	FastCGIHeader hdrParam = makeHeader(FCGI_PARAMS, 1, 
		bSharedParams.size() + bParams.size() + params.size(), 0);
	pBackend->send((const char*)&hdrParam, sizeof(FastCGIHeader));
	
	//shared params
	if(!bSharedParams.empty())
		pBackend->send(bSharedParams);
	
	//extension params
	if(!bParams.empty())
		pBackend->send(bParams);
		
	//local params
	if(!params.empty())
		pBackend->send(params);
	
	//params end
	FastCGIHeader endParam = makeHeader(FCGI_PARAMS, 1, 0, 0);
	pBackend->send((const char*)&endParam, sizeof(FastCGIHeader));
	
	//stdin bein
	if(pRequest && !pRequest->empty()){
		FastCGIHeader inh = makeHeader(FCGI_STDIN, 1, pRequest->size(), 0);
		pBackend->send((const char*)&inh, sizeof(FastCGIHeader));
		pBackend->send(pRequest->data(), pRequest->size());
	}
	
	//stdin end
	FastCGIHeader inend = makeHeader(FCGI_STDIN, 1, 0, 0);
	pBackend->send((const char*)&inend, sizeof(FastCGIHeader));
	
	return true;
}

bool Backend::response()
{
	Buffer *pBuffer = pBackend->getBuffer();
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
					if(body.offset >= 0){
						mResponse.append(body.ptr + body.offset, body.length);
					}else{
						mResponse.append(body.ptr + body.total + body.offset, body.length);
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

void Backend::addSharedParam(const char *name, int namelen, const char *value, int vallen)
{
	makeParam(bSharedParams, name, namelen, value, vallen);
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
