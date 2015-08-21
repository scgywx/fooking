#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>
#include "fooking.h"

NS_BEGIN

#define LOG_BUFFER_SIZE 	8192

#define LOG_LEVEL_ERROR		1
#define LOG_LEVEL_INFO		2
#define LOG_LEVEL_DEBUG		3

#define LOG(_fmt, ...)		Log::getInstance()->write(LOG_LEVEL_DEBUG, _fmt, ##__VA_ARGS__)
#define LOG_INFO(_fmt, ...)	Log::getInstance()->write(LOG_LEVEL_INFO, _fmt, ##__VA_ARGS__)
#define LOG_ERR(_fmt, ...)	Log::getInstance()->write(LOG_LEVEL_ERROR, _fmt, ##__VA_ARGS__)

class Log:
	public Object
{
private:
	Log():
		nfd(-1),
		nlv(0){}
public:
	static Log* getInstance(){
		if(!psObj){
			psObj = new Log();
		}
		return psObj;
	}
	void init(int lv, int fd);
	bool init(int lv, const char *file);
	void write(int level, const char *fmt, ...);
private:
	int nfd;
	int nlv;
private:
	static Log*	psObj;
};

NS_END