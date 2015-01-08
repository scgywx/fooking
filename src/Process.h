#pragma once
#include "Common.h"
#include "Object.h"

NS_BEGIN

#define CH_PIPE		0 //传递文件描述符
#define CH_RELOAD	1 //重新加载配置文件
#define CH_EXIT		2 //退出程序

typedef struct{
	uint_16	type;
	uint_16	pid;
	int		fd;
}ChannelMsg;

class Process:
	public Object
{
public:
	Process();
	~Process();
public:
	bool start();
	int getPipefd(){ return nPipefd; }
	int getPid(){ return nPid;}
	int send(ChannelMsg *ch);
	int recv(ChannelMsg *ch);
protected:
	virtual void proc() = 0;
protected:
	pid_t			nPid;
	int				nPipefd;
	bool			bRunning;
};
NS_END