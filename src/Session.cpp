#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "Session.h"
#include "Log.h"

NS_USING;

uint16_t Session::snMachine;

Session::Session(uint16_t pid, uint16_t fd):
	nPid(pid),
	nMachine(snMachine),
	nFd(fd)
{
	time_t t;
	time(&t);
	nTime = static_cast<uint32_t>(t);
	
	sprintf(sId, "%08x%04x%04x%04x", nTime, nMachine, nPid, nFd);
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

void Session::init()
{
	//read int16
	int rfd = open("/dev/urandom", O_RDONLY);
	if(rfd > 0){
		int n = read(rfd, &snMachine, sizeof(uint16_t));
		close(rfd);
		if(n == sizeof(uint16_t)){
			return ;
		}
	}else{
		LOG("can't open /dev/urandom, err=%s", strerror(errno));
	}
	
	//read
	srand((int)time(0));
	snMachine = rand() & 0xFFFF;
}

