#pragma once
#include "fooking.h"

NS_BEGIN

#define CH_RELOAD	1 //重新加载配置文件
#define CH_EXIT		2 //退出程序

typedef struct{
	uint16_t type;
	uint16_t from;
	uint32_t len;
	char data[0];
}PipeMsg;

class Process:
	public Object
{
public:
	Process();
	virtual ~Process();
public:
	bool start();
	int getPipefd() const{
		return nPipefd;
	}
	int getPid() const{
		return nPid;
	}
	void setPipe(int fds[2]){
		arrPipes[0] = fds[0];
		arrPipes[1] = fds[1];
	}
protected:
	virtual void proc() = 0;
protected:
	pid_t			nPid;
	int				nPipefd;
	int				arrPipes[2];
	bool			bRunning;
};
NS_END