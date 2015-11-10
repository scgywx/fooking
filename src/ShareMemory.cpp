#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "ShareMemory.h"
#include "Log.h"

NS_USING;

#define HAVE_MMAP 1

#if (HAVE_MMAP)
#include <sys/mman.h>
int ShareMemory::alloc(ShareMemoryInfo *shm)
{
	shm->addr = mmap(NULL, shm->size, PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0);
	if(shm->addr == MAP_FAILED) {
		LOG_ERR("mmap(MAP_ANON|MAP_SHARED, %uz) failed", shm->size);
		return -1;
	}
	return 0;
}

void ShareMemory::free(ShareMemoryInfo *shm)
{
	if (munmap(shm->addr, shm->size) == -1) {
		LOG_ERR("munmap(%p, %uz) failed", shm->addr, shm->size);
	}
}

#elif (HAVE_SYSVSHM)
#include <sys/ipc.h>
#include <sys/shm.h>
int ShareMemory::alloc(ShareMemoryInfo *shm)
{
	int  id;
	
	id = shmget(IPC_PRIVATE, shm->size, (SHM_R|SHM_W|IPC_CREAT));

	if (id == -1) {
		LOG_ERR("shmget(%uz) failed", shm->size);
		return -1;
	}

	LOG_INFO("shmget id: %d", id);

	shm->addr = shmat(id, NULL, 0);

	if (shm->addr == (void *) -1) {
		LOG_ERR("shmat() failed");
	}

	if (shmctl(id, IPC_RMID, NULL) == -1) {
		LOG_ERR("shmctl(IPC_RMID) failed");
	}

	return (shm->addr == (void *) -1) ? -1 : 0;
}

void ShareMemory::free(ShareMemoryInfo *shm)
{
	if (shmdt(shm->addr) == -1) {
		LOG_ERR("shmdt(%p) failed", shm->addr);
	}
}
#endif