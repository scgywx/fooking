#pragma once
#include "Common.h"

NS_BEGIN
class Utils
{
private:
	Utils(){}
public:
	static SocketOption parseSocket(const char *string);
	static int randInt(int min, int max);
};
NS_END