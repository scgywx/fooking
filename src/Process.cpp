#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "Process.h"
#include "Log.h"

NS_USING;

Process::Process()
{
}

Process::~Process()
{
}

bool Process::start()
{
	pid_t pid = fork();
	switch(pid){
	case -1:
		return false;
	case 0:
		//child;
		nPid = getpid();
		nPipefd = arrPipes[1];
		proc();
		exit(0);
		break;
	default:
		//master
		nPid = pid;
		nPipefd = arrPipes[0];
		break;
	}
	
	return true;
}