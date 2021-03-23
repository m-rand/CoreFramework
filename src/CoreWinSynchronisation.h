

#ifndef CoreWinSynchronisation_H 

#define CoreWinSynchronisation_H


#include "windows.h"
#include "CoreBase.h"



/******************************************************************************
 *
 *  CAS operations
 *  
 *****************************************************************************/

CORE_INLINE CoreBOOL
__CoreAtomic_compareAndSwap32_barrier(
    volatile CoreINT_S32 * mem , CoreINT_S32 oldVal, CoreINT_S32 newVal
)
{
    CoreINT_U32 _oldVal = InterlockedCompareExchange(
        (volatile LONG *) mem, (LONG) newVal, (LONG) oldVal
    );
    return (_oldVal == oldVal) ? true : false;
}
CORE_INLINE CoreBOOL
__CoreAtomic_compareAndSwapPtr_barrier(
    void * volatile * mem, void * oldVal, void * newVal
)
{
    PVOID _oldVal = InterlockedCompareExchangePointer(
        (PVOID volatile *) mem, (PVOID) newVal, (PVOID) oldVal
    );
    return (_oldVal == oldVal) ? true : false;
}
CORE_INLINE CoreINT_S32
__CoreAtomic_increment32(volatile CoreINT_S32 * value)
{
    return (unsigned int)InterlockedIncrement((volatile LONG *) value);
}
CORE_INLINE CoreINT_S32
__CoreAtomic_decrement32(volatile CoreINT_S32 * value)
{
    return (unsigned int)InterlockedDecrement((volatile LONG *) value);
}
CORE_INLINE void
__CoreAtomic_memoryBarrier(voide)
{
    MemoryBarrier();
}




/******************************************************************************
 *
 *  Locks
 *  
 *****************************************************************************/

typedef LONG CoreWinSpinLock;
typedef CoreWinSpinLock CoreSpinLock;


#define CORE_SPIN_LOCK_INIT 0

#ifndef CORE_WIN_SPINLOCK_LIMIT
#define CORE_WIN_SPINLOCK_LIMIT 100

CORE_INLINE CoreBOOL CoreSpinLock_init(CoreSpinLock * lock) 
{
    *lock = 0;
    return true;    
}

CORE_INLINE void CoreSpinLock_lock(volatile CoreSpinLock * lock) 
{
	while (InterlockedCompareExchange((LONG volatile *)lock, ~0, 0) != 0)
	{
        Sleep(0);
    }
/*    CoreINT_U32 count = 0;
    
    while (InterlockedCompareExchange((LONG volatile *)lock, ~0, 0) != 0)
    {
        Sleep(0);
    }*/
/*    while (InterlockedExchange(lock, 1) != 0) 
    {
        if (count < CORE_WIN_SPINLOCK_LIMIT)
        {
            Sleep(0); // shed_yield();
        }
        else
        {
            Sleep(0); // 1ms
        }
        count++;
    }*/
}
#undef CORE_WIN_SPINLOCK_LIMIT
#endif

CORE_INLINE void CoreSpinLock_unlock(volatile CoreSpinLock * lock) 
{
    MemoryBarrier();
    *lock = 0;
}

CORE_INLINE void CoreSpinLock_cleanup(volatile CoreSpinLock * lock)
{
    CoreSpinLock_init(lock);
}


typedef struct CoreWinLock
{
	HANDLE mutex;
} CoreWinLock;
typedef CoreWinLock CoreLock;


CORE_INLINE CoreBOOL CoreLock_lock(CoreLock * me) 
{
	return (WaitForSingleObject(me->mutex, INFINITE) == WAIT_OBJECT_0);
}

CORE_INLINE CoreBOOL CoreLock_unlock(CoreLock * me) 
{
	return ReleaseMutex(me->mutex);
}

static CoreBOOL CoreLock_init(CoreLock * me)
{
	me->mutex = CreateMutex(0, FALSE, 0);
	return (me->mutex != null);
}

static void CoreLock_cleanup(CoreLock * me)
{
    (void) CloseHandle(me->mutex);
}





typedef struct __CoreReadWriteLock CoreReadWriteLock;


CoreReadWriteLock * 
CoreReadWriteLock_create(CoreAllocatorRef allocator, CoreINT_U32 capacity);

void CoreReadWriteLock_destroy(
    CoreAllocatorRef allocator, CoreReadWriteLock * rwl);

void CoreReadWriteLock_lockRead(CoreReadWriteLock * rwl);

void CoreReadWriteLock_unlockRead(CoreReadWriteLock * rwl);

void CoreReadWriteLock_lockWrite(CoreReadWriteLock * rwl);

void CoreReadWriteLock_unlockWrite(CoreReadWriteLock * rwl);

#endif
