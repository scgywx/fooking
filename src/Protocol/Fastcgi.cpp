#include <string.h>
#include <stdio.h>
#include "Protocol.h"
#include "Log.h"
#include "Master.h"

NS_USING;

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

ProtocolFastcgi::ProtocolFastcgi()
{
	Master *master = Master::getInstance();
	Config *config = master->pConfig;
	char root[256];
	char name[256];
	int rootlen = sprintf(root, "%s/%s", config->sFastcgiRoot.c_str(), config->sFastcgiFile.c_str());
	int namelen = sprintf(name, "/%s", config->sFastcgiFile.c_str()); 
	
	//default
	makeParam(defParams, "REQUEST_METHOD", strlen("REQUEST_METHOD"), "POST", strlen("POST"));
	makeParam(defParams, "SCRIPT_FILENAME", strlen("SCRIPT_FILENAME"), root, rootlen);
	makeParam(defParams, "SCRIPT_NAME", strlen("SCRIPT_NAME"), name, namelen);
	makeParam(defParams, "DOCUMENT_ROOT", strlen("DOCUMENT_ROOT"), config->sFastcgiRoot.c_str(), config->sFastcgiRoot.size());
	//makeParam(defParams, "DOCUMENT_URI", strlen("DOCUMENT_URI"), name, namelen);
	
	//from config.lua
	FastcgiParams::const_iterator it;
	for(it = config->arFastcgiParams.begin(); it != config->arFastcgiParams.end(); ++it){
		std::string k = it->first;
		std::string v = it->second;
		makeParam(defParams, k.c_str(), k.size(), v.c_str(), v.size());
	}
}

ProtocolFastcgi::~ProtocolFastcgi()
{
	
}

int ProtocolFastcgi::pack(const char *inbuf, size_t inlen, Buffer &outbuf, Buffer *params)
{
	//header
	FastCGIHeader header = makeHeader(FCGI_BEGIN_REQUEST, 1, sizeof(FastCGIBeginRequest), 0);
	outbuf.append((const char*)&header, sizeof(FastCGIHeader));
	
	//begin request
	FastCGIBeginRequest begin = makeBeginRequest(FCGI_RESPONDER, 0);
	outbuf.append((const char*)&begin, sizeof(FastCGIBeginRequest));
	
	//params
	Buffer extParams;
	if(params && params->size()){
		extParams.append(*params);
	}
	if(inbuf && inlen){
		char clbuf[32];
		int clsize = sprintf(clbuf, "%d", inlen);
		makeParam(extParams, "CONTENT_LENGTH", strlen("CONTENT_LENGTH"), clbuf, clsize);
	}
	FastCGIHeader hdrParam = makeHeader(FCGI_PARAMS, 1, defParams.size() + extParams.size(), 0);
	outbuf.append((const char*)&hdrParam, sizeof(FastCGIHeader));
	outbuf.append(defParams);
	outbuf.append(extParams);
	
	//end params
	FastCGIHeader endParam = makeHeader(FCGI_PARAMS, 1, 0, 0);
	outbuf.append((const char*)&endParam, sizeof(FastCGIHeader));
	
	//stdin
	if(inbuf && inlen){
		FastCGIHeader inh = makeHeader(FCGI_STDIN, 1, inlen, 0);
		outbuf.append((const char*)&inh, sizeof(FastCGIHeader));
		outbuf.append(inbuf, inlen);
	}
	
	//end stdin
	FastCGIHeader inend = makeHeader(FCGI_STDIN, 1, 0, 0);
	outbuf.append((const char*)&inend, sizeof(FastCGIHeader));
	
	return 0;
}

int ProtocolFastcgi::unpack(const char *inbuf, size_t inlen, Buffer &outbuf)
{
	size_t offset = 0;
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
		LOG("version=%d, type=%d, requestid=%d, contentLength=%d, paddingLength=%d, reserved=%d, data=%s",
			header->version,
			header->type,
			requestId,
			contentLength,
			header->paddingLength,
			header->reserved,
			inbuf + offset + sizeof(FastCGIHeader));
		
		//check size
		if(offset + lineSize  > inlen){
			break;
		}else{
			offset+= lineSize;
		}
		
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
							outbuf.append(bodyptr + start, msglen);
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
				break;
			}
		}
	}
	
	return offset;
}

ProtocolFastcgi::FastCGIHeader ProtocolFastcgi::makeHeader(int type, int requestId, int contentLength, int paddingLength)
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

ProtocolFastcgi::FastCGIBeginRequest ProtocolFastcgi::makeBeginRequest(int role, int flag)
{
	FastCGIBeginRequest body;
    body.roleB1 = (unsigned char) ((role >>  8) & 0xff);
    body.roleB0 = (unsigned char) (role         & 0xff);
    body.flags  = (unsigned char) ((flag) ? 1 : 0);
    memset(body.reserved, 0, sizeof(body.reserved));
    return body;
}

int	ProtocolFastcgi::makeParam(Buffer &buf, const char *name, int namelen, const char *value, int vallen)
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
