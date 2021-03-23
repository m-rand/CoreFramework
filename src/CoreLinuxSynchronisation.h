

#ifndef CoreLinuxSynchronisation_H 

#define CoreLinuxSynchronisation_H


#include <errno.h>
#include <pthread.h>
#include "CoreBase.h"



/******************************************************************************
 *
 *  CAS operations
 *  
 *****************************************************************************/

#if defined(__GNUC__) && ((__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 1)))
// see http://gcc.gnu.org/onlinedocs/gcc-4.1.0/gcc/Atomic-Builtins.html

CORE_INLINE CoreBOOL
__CoreAtomic_compareAndSwap32_barrier(
    volatile CoreINT_S32 * mem, CoreINT_S32 oldVal, CoreINT_S32 newVal
)
{
    return __sync_bool_compare_and_swap(mem, oldVal, newVal);
}
CORE_INLINE CoreBOOL
__CoreAtomic_compareAndSwapPtr_barrier(
    void * volatile * mem, void * oldVal, void * newVal
)
{
    return __sync_bool_compare_and_swap(mem, oldVal, newVal);
}
CORE_INLINE CoreINT_S32
__CoreAtomic_increment32(volatile CoreINT_S32 * value)
{
    return __sync_fetch_and_add(value, 1);
}
CORE_INLINE CoreINT_S32
__CoreAtomic_decrement32(volatile CoreINT_S32 * value)
{
    return __sync_fetch_and_sub(value, 1);
}
CORE_INLINE void
__CoreAtomic_memoryBarrier(voide)
{
    __sync_synchronize();
}

#else

CORE_INLINE CoreBOOL
__CoreAtomic_compareAndSwap32_barrier(
    volatile CoreINT_S32 * mem, CoreINT_S32 oldVal, CoreINT_S32 newVal
)
{
    return !atomic_compare_and_exchange_bool_rel(mem, newVal, oldVal);
}
CORE_INLINE CoreBOOL
__CoreAtomic_compareAndSwapPtr_barrier(
    void * volatile * mem, void * oldVal, void * newVal
)
{
    return !atomic_compare_and_exchange_bool_rel(mem, newVal, oldVal);
}
CORE_INLINE CoreINT_S32
__CoreAtomic_increment32(volatile CoreINT_S32 * value)
{
    atomic_increment(value);
    return *value;
}
CORE_INLINE CoreINT_S32
__CoreAtomic_decrement32(volatile CoreINT_S32 * value)
{
    atomic_decrement(value);
    return *value;
}
CORE_INLINE void
__CoreAtomic_memoryBarrier(voide)
{
    atomic_full_barrier();
}
#endif
 
 
 
 
/******************************************************************************
 *
 *  Locks
 *  
 *****************************************************************************/

typedef struct CoreLinuxLock
{
    pthread_mutex_t mutex;
} CoreLinuxLock;


typedef CoreLinuxLock CoreLock;
typedef CoreLinuxLock CoreSpinLock;
//typedef pthread_spinlock_t CoreSpinLock;


#ifndef PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP 
#define PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP \
  {0, 0, 0, PTHREAD_MUTEX_ADAPTIVE_NP, __LOCK_INITIALIZER}
#endif


//#define CORE_SPIN_LOCK_INIT { PTHREAD_MUTEX_INITIALIZER }
#define CORE_SPIN_LOCK_INIT { PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP }

CORE_INLINE CoreBOOL CoreLock_init(CoreLock * me) 
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ADAPTIVE_NP);
    return (pthread_mutex_init(&me->mutex, &attr) == 0);
}

CORE_INLINE void CoreLock_lock(CoreLock * me) 
{
    pthread_mutex_lock(&me->mutex);
}

CORE_INLINE void CoreLock_unlock(CoreLock * me) 
{
    pthread_mutex_unlock(&me->mutex);
}

static void CoreLock_cleanup(CoreLock * me)
{
    int res;
    
    do 
    {
        res = pthread_mutex_destroy(&me->mutex);		
    }
    while (res == EBUSY);
}

CORE_INLINE CoreBOOL CoreSpinLock_init(CoreSpinLock * me) 
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ADAPTIVE_NP);
    return (pthread_mutex_init(&me->mutex, NULL) == 0);
    /**me = 0;
    return true;*/
    //return pthread_spin_init(me, 0);
}

CORE_INLINE void CoreSpinLock_lock(volatile CoreSpinLock * me) 
{
    CoreSpinLock * sl = (CoreSpinLock *) me; // suppress warnings
    pthread_mutex_lock(&sl->mutex);
/*  while (!CORE_ATOMIC_COMPARE_AND_SWAP_PTR((CoreINT_U32 volatile *) me, 1, 0))
    {
        sched_yield();
    }*/
    //pthread_spin_lock(me);
}

CORE_INLINE void CoreSpinLock_unlock(volatile CoreSpinLock * me) 
{
    CoreSpinLock * sl = (CoreSpinLock *) me; // suppress warnings
    pthread_mutex_unlock(&sl->mutex);
    //*me = 0;
    //pthread_spin_unlock(me);
}

CORE_INLINE void CoreSpinLock_cleanup(CoreSpinLock * me)
{
    int res;
    
    do 
    {
        res = pthread_mutex_destroy(&me->mutex);		
    }
    while (res == EBUSY);
    //pthread_spin_destroy(me);
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
