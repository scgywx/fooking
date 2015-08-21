#pragma once

#include <string>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#define NS_NAME				fooking
#define NS_BEGIN			namespace NS_NAME{
#define NS_END				}
#define NS_USING			using namespace NS_NAME

#define zmalloc				malloc
#define zfree				free
#define zrealloc			realloc

#define SOCKET_NONE			0
#define SOCKET_TCP			1
#define SOCKET_UNIX			2

#define SID_LENGTH			20
#define SID_FULL_LEN		21

/* Anti-warning macro... */
#define NOTUSED(V) 			((void) V)

typedef struct{
	int		argc;
	char**	argv;
	char**	argvCopy;
	char*	argvLast;
}ProcessInfo;

extern ProcessInfo proc;

NS_BEGIN
	//base class
	class Object
	{
	public:
		Object(){}
		virtual ~Object(){}
	};
	
	typedef unsigned char	uint_8;
	typedef unsigned short	uint_16;
	typedef unsigned int	uint_32;

	//socket类型
	typedef struct{
		int		type;
		union{
			struct{
				short		tcp_port;
				char		tcp_host[58];
			};
			struct{
				char		unix_name[60];
			};
		};
	}SocketOption;
	
	namespace net{
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
	}
	
	namespace utils{
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