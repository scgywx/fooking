#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>
#include "Common.h"

NS_BEGIN

#define LOG_BUFFER_SIZE 	1024

#define LOG_LEVEL_ERROR		1
#define LOG_LEVEL_INFO		2
#define LOG_LEVEL_DEBUG		3

#define LOG(_fmt, ...)		Log::write(LOG_LEVEL_DEBUG, _fmt, ##__VA_ARGS__)
#define LOG_INFO(_fmt, ...)	Log::write(LOG_LEVEL_INFO, _fmt, ##__VA_ARGS__)
#define LOG_ERR(_fmt, ...)	Log::write(LOG_LEVEL_ERROR, _fmt, ##__VA_ARGS__)

class Log
{
private:
	Log(){}
public:
	static void init(int lv, int fd);
	static bool init(int lv, const char *file);
	static void write(int level, const char *fmt, ...);
private:
	static int g_logfd;
	static int g_loglv;
};

NS_END