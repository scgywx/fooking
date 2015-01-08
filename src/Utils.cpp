#include <iostream>
#include <string.h>
#include <stdlib.h>
#include "Utils.h"

NS_USING;

SocketOption Utils::parseSocket(const char *string)
{
	char *str = (char*)string;
	int len = strlen(string);
	SocketOption opt;
	memset(&opt, 0, sizeof(SocketOption));
	
	if(strncmp(str, "unix:", 5) == 0){//unix
		opt.type = SOCKET_UNIX;
		memcpy(opt.unix_name, str + 5, len - 5);
		return opt;
	}
	
	//tcp tok
	int pos = 0;
	if(strncmp(str, "tcp://", 6) == 0){
		pos = 6;
	}
	
	char *sep = strchr(str + pos, ':');
	if(!sep){
		return opt;
	}
	
	//host length
	int nhost = sep - str + pos;
	
	//port
	int port = atoi(sep + 1);
	if(port < 1){
		return opt;
	}
	
	opt.type = SOCKET_TCP;
	opt.tcp_port = port;
	memcpy(opt.tcp_host, str + pos, nhost);
	
	return opt;
}

int Utils::randInt(int a, int b)
{
	if(a == b){
		return a;
	}else if(a > b){
		int t = a;
		a = b;
		b = t;
	}

	int n = b - a + 1;
	return rand() % n + a;
}
