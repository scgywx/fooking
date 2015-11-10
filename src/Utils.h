#pragma once
#include "fooking.h"

NS_BEGIN

namespace utils
{
	inline short readNetInt16(const char *ptr)
	{
		short n = ((unsigned char)ptr[0] << 8) | (unsigned char)ptr[1];
		return n;
	}

	inline int readNetInt32(const char *ptr)
	{
		int n = (((unsigned char)ptr[0] & 0x7F) << 24) | 
				((unsigned char)ptr[1] << 16) | 
				((unsigned char)ptr[2] << 8) | 
				(unsigned char)ptr[3];
		return n;
	}

	inline void writeNetInt16(char *ptr, short n)
	{
		ptr[0] = (n >> 8) & 0xFF;
		ptr[1] = n & 0xFF;
	}

	inline void writeNetInt32(char *ptr, int n)
	{
		ptr[0] = (n >> 24) & 0x7F;
		ptr[1] = (n >> 16) & 0xFF;
		ptr[2] = (n >> 8) & 0xFF;
		ptr[3] = n & 0xFF;
	}
	
	inline int randInt(int a, int b)
	{
		int n = b - a + 1;
		return rand() % n + a;
	}
	
	void initProcTitle(int argc, char **argv);
	
	void setProcTitle(const char *title);
	
	SocketOption parseSocket(const char *string);
}

NS_END