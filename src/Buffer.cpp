#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include "Buffer.h"

NS_USING;

Buffer::Buffer():
	pBuffer(NULL),
	nTotal(0),
	nLength(0),
	nOffset(0)
{
}

Buffer::Buffer(size_t n):
	pBuffer(NULL),
	nTotal(0),
	nLength(0),
	nOffset(0)
{
	if(n){
		resize(n);
	}
}

Buffer::Buffer(const char *buffer, size_t n):
	pBuffer(NULL),
	nTotal(0),
	nLength(0),
	nOffset(0)
{
	if(n){
		resize(n);
		append(buffer, n);
	}
}

Buffer::Buffer(const Buffer &buf):
	pBuffer(NULL),
	nTotal(0),
	nLength(0),
	nOffset(0)
{
	size_t sz = buf.size();
	if(sz){
		resize(sz);
		append(buf.data(), sz);
	}
}

Buffer::~Buffer()
{
	zfree(pBuffer);
}

size_t Buffer::append(const char *buf, size_t len)
{
	if(!len){
		return nLength;
	}
	
	//计算空闲大小
	size_t tailFree = nTotal - nOffset - nLength;
	if(tailFree < len){
		if(tailFree + nOffset >= len){
			memcpy(pBuffer, pBuffer + nOffset, nLength);
			nOffset = 0;
		}else{
			resize(nTotal + len);
		}
	}

	memcpy(pBuffer + nOffset + nLength, buf, len);
	nLength+= len;
	
	//end char
	pBuffer[nOffset + nLength] = '\0';

	return nLength;
}

size_t Buffer::seek(size_t n)
{
	if(n >= nLength){
		nLength = 0;
		nOffset = 0;
	}else{
		nOffset+= n;
		nLength-= n;
	}
	
	return nLength;
}