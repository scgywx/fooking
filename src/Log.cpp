#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "Log.h"

NS_USING;

int Log::g_logfd = -1;
int Log::g_loglv = 0;

void Log::init(int lv, int fd)
{
	g_logfd = fd;
	g_loglv = lv;
}

bool Log::init(int lv, const char *file)
{
	if(!file){
		return false;
	}
	
	int fd = ::open(file, O_WRONLY | O_APPEND | O_CREAT, 0644);
	if(fd == -1){
		return false;
	}
	
	g_logfd = fd;
	g_loglv = lv;
	
	return true;
}

void Log::write(int level, const char *fmt, ...)
{
	static const char *levelstr[] = {"NONE", "ERROR" ,"INFO" ,"DEBUG"};
	va_list ap;
	char msg[LOG_BUFFER_SIZE];
	char buf[LOG_BUFFER_SIZE];
	
	if(g_logfd < 0){
		return ;
	}
	
	if(level > g_loglv){
		return ;
	}

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	
	struct timeval tv;
	gettimeofday(&tv, NULL);
	struct tm *t = localtime(&tv.tv_sec);
	int n = sprintf(buf, "%d-%02d-%02d %02d:%02d:%02d.%06d [%s][%d] %s\n", 
			t->tm_year + 1900,
			t->tm_mon + 1,
			t->tm_mday,
			t->tm_hour,
			t->tm_min,
			t->tm_sec,
			tv.tv_usec,
			levelstr[level],
			getpid(),
			msg);

	::write(g_logfd, buf, n);
}