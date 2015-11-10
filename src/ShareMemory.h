#pragma once
#include <string>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include "fooking.h"

NS_BEGIN

typedef struct{
	void *addr;
	uint32_t size;
}ShareMemoryInfo;
	
namespace ShareMemory
{
	int alloc(ShareMemoryInfo *shm);
	void free(ShareMemoryInfo *shm);
}

NS_END