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
	Session(uint_16 pid, uint_16 fd);
	Session(const char *sid);
public:
	const char* getId(){
		return sId;
	}
	uint_16	getFd(){
		return nFd;
	}
	uint_16 getPid(){
		return nPid;
	}
	uint_32 getTime(){
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
	uint_32					nTime;
	uint_16					nPid;
	uint_16					nMachine;
	uint_16					nFd;
	char					sId[SID_FULL_LEN];
private:
	static uint_16			snMachine;
};
NS_END