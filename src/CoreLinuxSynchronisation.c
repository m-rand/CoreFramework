


#include "CoreLinuxSynchronisation.h"
#include "CoreInternal.h"




/*
 * ReadWriteLock
 */
   
typedef struct ReadWriteLockEntry 
{
    pthread_t       thread;
    CoreINT_U32     count; // recursion count
} ReadWriteLockEntry;

typedef struct ReadWriteLockList 
{
    CoreINT_U32         count;
    ReadWriteLockEntry  *list; // inlined
} ReadWriteLockList;

struct __CoreReadWriteLock
{
    pthread_mutex_t mutex;
    pthread_cond_t readCondition;
    pthread_cond_t writeCondition;
    CoreINT_U32 capacity;
    ReadWriteLockList readers; // inlined
    ReadWriteLockList writers; // inlined
};







CORE_INLINE CoreBOOL
_CoreReadWriteLock_addThread(
    CoreReadWriteLock * rlw, ReadWriteLockList * list, pthread_t thread
)
{
    CoreINT_U32 idx;
    CoreBOOL result = false;
    
    //
    // Go through the list and find out whether the thread is already presented.
    // In such a case, just increase its (recursion) count.
    // 
    for (idx = 0; idx < list->count; idx++)
    {
        if (pthread_equal(list->list[idx].thread, thread))
        {
            list->list[idx].count++;
            result = true;
            break;
        }
    }
    
    //
    // The thread was not found -- add it to the list.
    //
    if (!result)
    {
        if (CORE_UNLIKELY(list->count >= rlw->capacity))
        {
            CORE_DUMP_MSG(
                CORE_LOG_CRITICAL,
                "CoreReadWriteLock <%p> error!: thread <%d> cannot be added "
                "to lock's list -- full capacity reached.", rlw, thread
            );
        }
        else
        {
            list->list[list->count].thread = thread;
            list->list[list->count].count = 1;
            list->count++;
            result = true;
        }
	}
	
	return result;
}


CORE_INLINE void
_CoreReadWriteLock_addReader(CoreReadWriteLock * rlw, pthread_t reader)
{
    if (!_CoreReadWriteLock_addThread(rlw, &rlw->readers, reader))
    {
        CORE_DUMP_MSG(
            CORE_LOG_CRITICAL,
            "CoreReadWriteLock <%p> error!: "
            "trying to lock for reader <%d> failed!", rlw, reader
        );
    }
}


CORE_INLINE void
_CoreReadWriteLock_addWriter(CoreReadWriteLock * rlw, pthread_t writer)
{
    if (!_CoreReadWriteLock_addThread(rlw, &rlw->writers, writer))
    {
        CORE_DUMP_MSG(
            CORE_LOG_CRITICAL,
            "CoreReadWriteLock <%p> error!: "
            "trying to lock for writer <%d> failed!", rlw, writer
        );
    }
}


CORE_INLINE void
_CoreReadWriteLock_removeThread(
    CoreReadWriteLock * rlw, 
    ReadWriteLockList * list, 
    pthread_t thread, 
    CoreBOOL ordered
)
{
    CoreINT_U32 idx;
    
    for (idx = 0; idx < list->count; idx++)
    {
        if (pthread_equal(list->list[idx].thread, thread))
        {
            //
            // If it is the last recursion access, remove it.
            //
            if (list->list[idx].count == 1)
            {
                //
                // If ordered required, list behavior is used;
                // otherwise it behaves like a collection.
                //
                if (ordered)
                {
                    memmove(
                        list->list + idx,
                        list->list + idx + 1,
                        (list->count - idx) * sizeof(list->list[0])
                    );
                }
                else
                {
                    list->list[idx] = list->list[list->count - 1];
                }
            }
            list->list[idx].count--;
            list->count--;
            break;
        }
    }
}


CORE_INLINE void
_CoreReadWriteLock_removeReader(CoreReadWriteLock * rlw, pthread_t reader)
{
    //
    // Readers are woken up by a broadcast, so we don't care about their order.
    // That's why we can use a quick, order-less remove.
    //
    return _CoreReadWriteLock_removeThread(rlw, &rlw->readers, reader, false);
}


CORE_INLINE void
_CoreReadWriteLock_removeWriter(CoreReadWriteLock * rlw, pthread_t writer)
{
    //
    // Writers are woken up in a one-by-one way, so we do care about their
    // order. That's why we use an ordered way of removal.
    //
    return _CoreReadWriteLock_removeThread(rlw, &rlw->writers, writer, true);
}



CORE_INLINE CoreBOOL 
_CoreReadWriteLock_isReader(CoreReadWriteLock * rlw, pthread_t thread)
{
    CoreBOOL result = false;
    CoreINT_U32 idx;
    
    for (idx = 0; idx < rlw->readers.count; idx++)
    {
        if (pthread_equal(rlw->readers.list[idx].thread, thread))
        {
            result = true;
            break;
        }
    }

    return result;
} 


CORE_INLINE CoreBOOL 
_CoreReadWriteLock_isOnlyReader(CoreReadWriteLock * rlw, pthread_t thread)
{
    return ((rlw->readers.count == 1) && 
            pthread_equal(rlw->readers.list[0].thread, thread));
}


CORE_INLINE CoreBOOL 
_CoreReadWriteLock_isWriter(CoreReadWriteLock * rlw, pthread_t thread)
{
    return ((rlw->writers.count > 0) && 
            pthread_equal(rlw->writers.list[0].thread, thread));
}


CORE_INLINE CoreBOOL 
_CoreReadWriteLock_canRead(CoreReadWriteLock * rlw, pthread_t reader)
{
    return ((rlw->writers.count == 0) || 
            pthread_equal(rlw->writers.list[0].thread, reader));
/*  if (_CoreReadWriteLock_isWriter(rlw, reader))    return true;
    if (rlw->writers.count > 0)                  return false;
    if (_CoreReadWriteLock_isReader(rlw, reader))    return true;
    if (rlw->writers.count > 0)                  return false;
    return true;*/
}


CORE_INLINE CoreBOOL 
_CoreReadWriteLock_canWrite(CoreReadWriteLock * rlw, pthread_t writer)
{
    if (_CoreReadWriteLock_isOnlyReader(rlw, writer))    return true;
    if (rlw->readers.count > 0)                      return false;
    if (rlw->writers.count == 0)                     return true;
    if (!_CoreReadWriteLock_isWriter(rlw, writer))       return false;
    return true;
}





CoreReadWriteLock * 
CoreReadWriteLock_create(CoreAllocatorRef allocator, CoreINT_U32 capacity)
{
    CoreReadWriteLock * result = null;
    CoreINT_U32 size = 0;
    
    
    size = sizeof(CoreReadWriteLock) + 2 * sizeof(ReadWriteLockList) 
        + 2 * capacity * sizeof(ReadWriteLockEntry);
    result = CoreAllocator_allocate(allocator, size);
    if (result != null)
    {
        CoreBOOL ok;
        
        result->capacity = capacity;
        result->readers.list = (ReadWriteLockEntry *) (CoreINT_U8 *) result 
            + sizeof(CoreReadWriteLock) + sizeof(ReadWriteLockList);
        result->writers.list = (ReadWriteLockEntry *) (CoreINT_U8 *) &result->readers 
            + capacity * sizeof(ReadWriteLockEntry) + sizeof(ReadWriteLockList);
        result->readers.count = 0;
        result->writers.count = 0;
        ok = (
            (pthread_mutex_init(&result->mutex, NULL) == 0)	&&
            (pthread_cond_init(&result->readCondition, NULL) == 0) &&
            (pthread_cond_init(&result->writeCondition, NULL) == 0)
        );
        if (!ok)
        {
            CoreAllocator_deallocate(allocator, result);
            result = null;
        }
    } 

    return result;
}    


void CoreReadWriteLock_destroy(
    CoreAllocatorRef allocator, CoreReadWriteLock * rwl)
{
    pthread_cond_destroy(&rwl->readCondition);
    pthread_cond_destroy(&rwl->writeCondition);
    pthread_mutex_destroy(&rwl->mutex);

    CoreAllocator_deallocate(allocator, rwl);
}


void 
CoreReadWriteLock_lockRead(CoreReadWriteLock * rwl)
{
    pthread_t reader = pthread_self();
    
    pthread_mutex_lock(&rwl->mutex);
    while (!_CoreReadWriteLock_canRead(rwl, reader))
    {
        pthread_cond_wait(&rwl->readCondition, &rwl->mutex);
    }
    _CoreReadWriteLock_addReader(rwl, reader);
    pthread_mutex_unlock(&rwl->mutex);
}


void 
CoreReadWriteLock_unlockRead(CoreReadWriteLock * rwl)
{
    pthread_t reader = pthread_self();
    
    pthread_mutex_lock(&rwl->mutex);
    _CoreReadWriteLock_removeReader(rwl, reader);
    if ((rwl->readers.count == 0) && (rwl->writers.count > 0))
    {
        // Wake up the _first_ writer.
        pthread_cond_signal(&rwl->writeCondition);
    }
    pthread_mutex_unlock(&rwl->mutex);
}


void 
CoreReadWriteLock_lockWrite(CoreReadWriteLock * rwl)
{
    pthread_t writer = pthread_self();
    
    pthread_mutex_lock(&rwl->mutex);
    _CoreReadWriteLock_addWriter(rwl, writer);
    while (!_CoreReadWriteLock_canWrite(rwl, writer))
    {
        pthread_cond_wait(&rwl->writeCondition, &rwl->mutex);
    }
    pthread_mutex_unlock(&rwl->mutex);
}


void 
CoreReadWriteLock_unlockWrite(CoreReadWriteLock * rwl)
{
    pthread_t writer = pthread_self();
    
    pthread_mutex_lock(&rwl->mutex);
    _CoreReadWriteLock_removeWriter(rwl, writer);
    if (rwl->writers.count == 0)
    {
        // There is no other writer, so wake up all readers.
        pthread_cond_broadcast(&rwl->readCondition);
    }
    else
    {
        // There is another writer, so wake him up!
        pthread_cond_signal(&rwl->writeCondition);
    }
    pthread_mutex_unlock(&rwl->mutex);
}
