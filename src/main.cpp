#include <iostream>
#include "Master.h"

NS_USING;

int main(int argc, char **argv)
{
	Master::getInstance()->start(argc, argv);
	return 0;
}