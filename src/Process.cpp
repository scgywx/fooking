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
	close(nPipefd);
}

bool Process::start()
{
	int fds[2];
	if(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1){
		return false;
	}
	
	for(int i = 0; i < 2; ++i){
		//set nonblock
		int flags = ::fcntl(fds[i], F_GETFL, 0);
		flags |= O_NONBLOCK;
		int ret = ::fcntl(fds[i], F_SETFL, flags);
		if(ret < 0){
			close(fds[0]);
			close(fds[1]);
			return false;
		}
	}

	pid_t pid = fork();
	switch(pid){
	case -1:
		close(fds[0]);
		close(fds[1]);
		return false;
	case 0:
		//child;
		close(fds[0]);
		nPid = getpid();
		nPipefd = fds[1];
		proc();
		exit(0);
		break;
	default:
		//master
		close(fds[1]);
		nPid = pid;
		nPipefd = fds[0];
		break;
	}
	
	return true;
}

int Process::send(ChannelMsg *ch)
{
	struct iovec	iov[1];
	struct msghdr	msg;

	msg.msg_control = NULL;
	msg.msg_controllen = 0;

	iov[0].iov_base = (char *) ch;
	iov[0].iov_len = sizeof(ChannelMsg);

	msg.msg_flags = 0;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	int n = ::sendmsg(nPipefd, &msg, 0);
	LOG("sendmsg type=%d, len=%d", ch->type, n);
	if (n == -1) {
		if (errno == EAGAIN) {
			return EAGAIN;
		}

		LOG_ERR("sendmsg() errno=%d, error=%s", errno, strerror(errno));
		return -1;
	}
	
	return 0;
}

int Process::recv(ChannelMsg *ch)
{
	struct iovec	iov[1];
	struct msghdr	msg;

	union {
		struct cmsghdr  cm;
		char            space[CMSG_SPACE(sizeof(int))];
	} cmsg;

	iov[0].iov_base = (char *)ch;
	iov[0].iov_len = sizeof(ChannelMsg);

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_control = (caddr_t) &cmsg;
	msg.msg_controllen = sizeof(cmsg);

	int n = ::recvmsg(nPipefd, &msg, 0);
	if (n == -1) {
		if (errno == EAGAIN) {
			return EAGAIN;
		}
		LOG("recvmsg() error");
		return -1;
	}else if(n == 0){
		LOG_ERR("recvmsg() returned zero");
		return -1;
	}

	if ((size_t)n < sizeof(ChannelMsg)) {
		LOG_ERR("recvmsg() returned not enough data: %d", n);
		return -1;
	}

	if (msg.msg_flags & (MSG_TRUNC|MSG_CTRUNC)) {
		LOG_ERR("recvmsg() truncated data");
	}

	return 0;
}