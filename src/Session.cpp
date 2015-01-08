#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "Session.h"
#include "Log.h"

NS_USING;

uint_16 Session::snMachine;

Session::Session():
	nPid(0),
	nMachine(0),
	nFd(0)
{
	memset(sId, 0, SID_FULL_LEN);
}

Session::Session(uint_16 pid, uint_16 fd):
	nPid(pid),
	nMachine(snMachine),
	nFd(fd)
{
	time(&nTime);
	sprintf(sId, "%08x%04x%04x%04x", static_cast<unsigned int>(nTime), nMachine, nPid, nFd);
	sId[SID_LENGTH] = '\0';
}

Session::Session(const char *sid)
{
	char stime[16] = {0};
	char smachine[16] = {0};
	char spid[16] = {0};
	char sfd[16] = {0};
	
	//sid
	memcpy(sId, sid, SID_LENGTH);
	sId[SID_LENGTH] = '\0';
	
	//time
	memcpy(stime, sid, 8);
	nTime = strtol(stime, NULL, 16);
	
	//machine
	memcpy(smachine, sid + 8, 4);
	nMachine = strtol(smachine, NULL, 16);
	
	//pid
	memcpy(spid, sid + 12, 4);
	nPid = strtol(spid, NULL, 16);
	
	//fd
	memcpy(sfd, sid + 16, 4);
	nFd = strtol(sfd, NULL, 16);
}

bool Session::operator==(const Session&r) const
{
	if(r.nTime == nTime && 
		r.nMachine == nMachine &&
		r.nPid == nPid &&
		r.nFd == nFd)
	{
		return true;
	}
	return false;
}

void Session::init()
{
	//read int16
	int rfd = open("/dev/urandom", O_RDONLY);
	if(rfd > 0){
		int n = read(rfd, &snMachine, sizeof(uint_16));
		close(rfd);
		if(n == sizeof(uint_16)){
			return ;
		}
	}else{
		LOG("can't open /dev/urandom, err=%s", strerror(errno));
	}
	
	//read
	srand((int)time(0));
	snMachine = rand() & 0xFFFF;
}

