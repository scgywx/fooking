#pragma once
#include <string>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "fooking.h"

NS_BEGIN

class Session
{
public:
	Session(){}
	Session(uint16_t mask);
public:
	const char* getId(){
		return sId;
	}
public:
	static void init(uint16_t serverid);
private:
	char				sId[SID_FULL_LEN];
private:
	static uint16_t		snServerId;
};
NS_END