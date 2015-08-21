#pragma once
#include <string.h>
#include "fooking.h"

NS_BEGIN

#define BUFF_SIZE	1024

class Buffer
{
public:
	Buffer();
	explicit Buffer(size_t n);
	Buffer(const char *buffer, size_t size);
	Buffer(const Buffer &buf);
	~Buffer();
private:
	Buffer& operator=(const Buffer&);
public:
	size_t append(const char *buf, size_t len);
	
	size_t append(const char *buf){
		return append(buf, strlen(buf));
	}
	
	size_t append(const Buffer &buf){
		return append(buf.data(), buf.size());
	}
	
	size_t append(const Buffer *buf){
		return append(buf->data(), buf->size());
	}
	
	size_t size() const{
		return nLength;
	}
	
	bool empty() const{
		return nLength == 0; 
	}
	
	char *data() const{
		return pBuffer + nOffset;
	}
	
	void clear(){
		nLength = nOffset = 0;
	}
	
	size_t seek(size_t n);
private:
	void resize(size_t n){
		if(nTotal < n){
			//size + 1 end char(supported string)
			pBuffer = (char *)zrealloc(pBuffer, n + 1);
			nTotal = n;
		}
	}
private:
	char*	pBuffer;
	size_t	nTotal;//总长度
	size_t	nLength;//数据长度
	size_t	nOffset;//偏移位置
};
NS_END