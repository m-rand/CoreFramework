

#include "CoreWinSynchronisation.h"

struct __CoreReadWriteLock
{
	HANDLE mutex;
	HANDLE readCond;
	HANDLE writeCond;
	int readerCount;
};


void CoreReadWriteLock_lockRead(CoreReadWriteLock * me)
{
	HANDLE ar[2] = {me->readCond, me->mutex};
	
	WaitForMultipleObjects(2, ar, TRUE, INFINITE);
	me->readerCount++;
	ResetEvent(me->writeCond);
	ReleaseMutex(me->mutex);
	
/*	assert(WaitForSingleObject(me->writeCond, 0) == WAIT_TIMEOUT);
	assert(me->readerCount > 0);*/
}

void CoreReadWriteLock_lockWrite(CoreReadWriteLock * me)
{
	HANDLE ar[2] = {me->writeCond, me->mutex};
	
	WaitForMultipleObjects(2, ar, TRUE, INFINITE);
	ResetEvent(me->readCond);
	ReleaseMutex(me->mutex);
	
/*	assert(WaitForSingleObject(me->readCond, 0) == WAIT_TIMEOUT);
	assert(me->readerCount == 0);*/
}

static void _CoreReadWriteLock_unlock(CoreReadWriteLock * me)
{
	WaitForSingleObject(me->mutex, INFINITE);
	if (me->readerCount > 0)
	{
		if (--me->readerCount == 0)
		{
			SetEvent(me->writeCond);
		}
	}
	else
	{
		SetEvent(me->writeCond);
		SetEvent(me->readCond);
	}
	ReleaseMutex(me->mutex);
}

void CoreReadWriteLock_unlockRead(CoreReadWriteLock * me)
{
	_CoreReadWriteLock_unlock(me);	
}

void CoreReadWriteLock_unlockWrite(CoreReadWriteLock * me)
{
	_CoreReadWriteLock_unlock(me);	
}

CoreBOOL CoreReadWriteLock_init(CoreReadWriteLock * me)
{
	me->readerCount = 0;
	me->mutex = CreateMutex(0, FALSE, 0);
	me->readCond = CreateEvent(0, TRUE, TRUE, 0);
	me->writeCond = CreateEvent(0, FALSE, TRUE, 0);
	
	return ((me->mutex != null) && (me->readCond != null) &&
			(me->writeCond != null)); 
}

CoreReadWriteLock * CoreReadWriteLock_create(
    CoreAllocatorRef allocator, CoreINT_U32 capacity)
{
    CoreReadWriteLock * me;
    
    me = (CoreReadWriteLock *) CoreAllocator_allocate(
        allocator,
        sizeof(CoreReadWriteLock)
	);
    if (me != null)
	{
		if (!CoreReadWriteLock_init(me))
		{
            CoreAllocator_deallocate(allocator, me);
            me = null;
        }
	}
    
	return me;
}


void CoreReadWriteLock_destroy(
    CoreAllocatorRef allocator, CoreReadWriteLock * rwl)
{
	(void) CloseHandle(rwl->mutex);
	(void) CloseHandle(rwl->readCond);
	(void) CloseHandle(rwl->writeCond);

	CoreAllocator_deallocate(allocator, rwl);
}

