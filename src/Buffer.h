#pragma once
#include <string.h>
#include "Common.h"

NS_BEGIN

#define BUFF_SIZE	1024

class Buffer
{
public:
	Buffer();
	Buffer(size_t n);
	Buffer(const char *buffer, size_t size);
	Buffer(Buffer &buf);
	~Buffer();
public:
	size_t append(const char *buf, size_t len);
	size_t append(const char *buf){ return append(buf, strlen(buf)); }
	size_t append(Buffer &buf){ return append(buf.data(), buf.size()); }
	size_t append(Buffer *buf){ return append(buf->data(), buf->size()); }
	size_t size() const{ return nLength;}
	char *data(){ return pBuffer + nOffset;}
	void clear(){ nLength = nOffset = 0;}
	size_t seek(size_t n);
private:
	void resize(size_t n);
private:
	char*	pBuffer;
	size_t	nTotal;//总长度
	size_t	nLength;//数据长度
	size_t	nOffset;//偏移位置
};
NS_END