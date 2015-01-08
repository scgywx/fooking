#pragma once
#include <map>
#include <string>
#include "Common.h"
#include "Buffer.h"

#define PROTO_ERR		-1

NS_BEGIN
class IProtocol
{
public:
	virtual ~IProtocol(){};
public:
	virtual int pack(const char *inbuf, size_t inlen, Buffer &outbuf, Buffer *params = NULL) = 0;
	virtual int unpack(const char *inbuf, size_t inlen, Buffer &outbuf) = 0;
	virtual int makeParam(Buffer &buf, const char *name, int namelen, const char *value, int vallen){ return 0;};
};

//fastcgi
class ProtocolFastcgi:
	public IProtocol
{
public:
	typedef std::map<std::string, std::string> NameValue;
	typedef struct{
		unsigned char version;
		unsigned char type;
		unsigned char requestIdB1;
		unsigned char requestIdB0;
		unsigned char contentLengthB1;
		unsigned char contentLengthB0;
		unsigned char paddingLength;
		unsigned char reserved;
	}FastCGIHeader;
	
	typedef struct{
		unsigned char roleB1;
		unsigned char roleB0;
		unsigned char flags;
		unsigned char reserved[5];
	}FastCGIBeginRequest;
	
	typedef struct {
		unsigned char appStatusB3;
		unsigned char appStatusB2;
		unsigned char appStatusB1;
		unsigned char appStatusB0;
		unsigned char protocolStatus;
		unsigned char reserved[3];
	}FastCGIEndRequest;
public:
	ProtocolFastcgi();
	~ProtocolFastcgi();
public:
	virtual int 		pack(const char *inbuf, size_t inlen, Buffer &outbuf, Buffer *params = NULL);
	virtual int 		unpack(const char *inbuf, size_t inlen, Buffer &outbuf);
	virtual int			makeParam(Buffer &buf, const char *name, int namelen, const char *value, int vallen);
private:
	FastCGIHeader		makeHeader(int type, int requestid, int contentLength, int paddingLength);
	FastCGIBeginRequest	makeBeginRequest(int role, int flag);
private:
	Buffer				defParams;
};
NS_END