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
	if(n > 0){
		resize(n);
	}
}

Buffer::Buffer(const char *buffer, size_t n):
	pBuffer(NULL),
	nTotal(0),
	nLength(0),
	nOffset(0)
{
	if(n > 0){
		resize(n);
		append(buffer, n);
	}
}

Buffer::Buffer(Buffer &buf):
	pBuffer(NULL),
	nTotal(0),
	nLength(0),
	nOffset(0)
{
	if(buf.size() > 0){
		resize(buf.size());
		append(buf.data(), buf.size());
	}
}

Buffer::~Buffer()
{
	free(pBuffer);
}

size_t Buffer::append(const char *buf, size_t len)
{
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

void Buffer::resize(size_t n)
{
	if(nTotal < n){
		pBuffer = (char *)zrealloc(pBuffer, n);
		nTotal = n;
	}
}