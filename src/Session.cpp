#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "Session.h"
#include "Log.h"

NS_USING;

uint16_t Session::snServerId;

/**
 * 31位时间戳(最大可表示到2038年)
 * 10位毫秒
 * 13位服务器ID(最大可表示8191台服务器)
 * 10位自增id(最大值是1023，超出从0开始重计)
 * 共64位，每秒可生成100w条不同ID
 */
Session::Session(uint16_t mask)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	int ms = tv.tv_usec / 1000;
	
	uint32_t int64hi, int64lo;
	int64hi = (tv.tv_sec << 1) | ((ms & 0x200) >> 9);
	int64lo = ((ms & 0x1ff) << 23) | (snServerId << 10) | (mask & 0x3ff);
	
	snprintf(sId, SID_FULL_LEN, "%08x%08x", int64hi, int64lo);
}

void Session::init(uint16_t serverid)
{
	snServerId = serverid & 0x1FFF;
}

