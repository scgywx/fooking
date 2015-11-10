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

NS_END