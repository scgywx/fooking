#pragma once
#include "fooking.h"

NS_BEGIN

#define CH_RELOAD	1 //重新加载配置文件
#define CH_EXIT		2 //退出程序

typedef struct{
	uint16_t type;
	uint16_t from;
}ChannelMsg;

class Process:
	public Object
{
public:
	Process();
	virtual ~Process();
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