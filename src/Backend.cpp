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

NS_USING;

Buffer Backend::bSharedParams;

Backend::Backend(EventLoop *loop, Connection *client, Buffer *request):
	pLoop(loop),
	pClient(client),
	pBackend(NULL),
	pRequest(request)
{
	//TODO
}

Backend::~Backend()
{
	shutdown();
	delete pRequest;
}

bool Backend::run()
{
	Config *pConfig = Config::getInstance();
	int r = utils::randInt(1, pConfig->nMaxBackendWeights);
	int nserver = pConfig->arrBackendServer.size();
	int start = utils::randInt(0, nserver - 1);
	int old = start;
	
	do{
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
			
			return true;
		}
		
		if(++start >= nserver){
			start = 0;
		}
		
	}while(old != start);
	
	LOG_ERR("Can't found backend, Please check BACKEND_SERVER");
	
	return false;
}

void Backend::shutdown()
{
	if(pBackend){
		pBackend->close();
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
	int err = conn->getError();
	if(err){
		LOG("backend error=%d, errstr=%s", err, strerror(err));
	}else{
		LOG("backend close, conn=%p", conn);
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
		
		FastCGIHeader *header = (FastCGIHeader*)(inbuf + offset);
		size_t requestId = (header->requestIdB1 << 8) | header->requestIdB0;
		size_t contentLength = (header->contentLengthB1 << 8) | header->contentLengthB0;
		int lineSize = sizeof(FastCGIHeader) + contentLength + header->paddingLength;
		
		//check size
		if(offset + lineSize  > inlen){
			break;
		}
		
		//log
		LOG("version=%d, type=%d, requestid=%d, contentLength=%d, paddingLength=%d, reserved=%d, data=%s",
			header->version,
			header->type,
			requestId,
			contentLength,
			header->paddingLength,
			header->reserved,
			inbuf + offset + sizeof(FastCGIHeader));
			
		//offset
		offset+= lineSize;
		
		const char *dataptr = (const char *)header + sizeof(FastCGIHeader);
		switch(header->type)
		{
			case FCGI_STDOUT:
			case FCGI_STDERR:
			{
				//读取body
				const char *lenptr = strstr(dataptr, "Content-Length: ");
				if(lenptr){
					lenptr+= (sizeof("Content-Length: ") - 1);
					int msglen = atoi(lenptr);
					const char *bodyptr = NULL;
					if(msglen && (bodyptr = strstr(lenptr, "\r\n\r\n"))){
						bodyptr+= 4;
						int bodylen = dataptr + contentLength - bodyptr;
						if(msglen <= bodylen){
							int start = bodylen - msglen;
							mResponse.append(bodyptr + start, msglen);
						}
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

