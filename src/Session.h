#pragma once
#include <string>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include "fooking.h"

NS_BEGIN

class Session
{
public:
	Session(){}
	Session(uint16_t pid, uint16_t fd);
	Session(const char *sid);
public:
	const char* getId(){
		return sId;
	}
	uint16_t	getFd(){
		return nFd;
	}
	uint16_t getPid(){
		return nPid;
	}
	uint32_t getTime(){
		return nTime;
	}
	bool operator == (const Session&r) const{
		return memcmp(sId, r.sId, SID_LENGTH) == 0;
	}
	bool operator < (const Session &r) const{
		return memcmp(sId, r.sId, SID_LENGTH) < 0;
	}
public:
	static void				init();
private:
	uint32_t			nTime;
	uint16_t			nPid;
	uint32_t			nMachine;
	uint32_t			nFd;
	char				sId[SID_FULL_LEN];
private:
	static uint16_t		snMachine;
};
NS_END