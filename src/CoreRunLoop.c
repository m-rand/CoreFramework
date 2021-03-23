

/*
    RunLoop: A general purpose Reactor-pattern-based class.
    What it does, is basically a demultiplexing of inputs and timers and
    dispatching events to clients. However, compared to a simple Reactor 
    pattern, it provides several advanced features, like:
    - you can specify a mode in which a run loop runs -- and therefore 
        discriminate sources and timers that are not included in the mode.
    - modes can be nested -- you can put another mode into already existed mode,
        creating a tree of (sub)modes.
    - run loop generates notifications about its current state, which makes
        possible for run-loop's clients to do some necessary stuff, e.g.
        before going to sleep or after waking up.     
    
    (Can be roughly likened to a select() function.)   
    
    Notes:
    - one source can be installed on one run-loop only
    - each mode can contain several submodes
    - loop can run in only one mode (including submodes) at a time
    - all callbacks should be non-blocking (to keep run-loop responsive)
*/
    

#include <stdlib.h>
#include <time.h>
#include "CoreRunLoop.h"
#include "CoreRunLoopPriv.h"
#include "CoreSet.h"
#include "CoreArray.h"
#include "CoreDictionary.h"
#include "CoreString.h"
#include "CoreSynchronisation.h"

#if defined(__LINUX__)
#include <poll.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#elif defined(__WIN32__)
#include "windows.h"
#include "winbase.h"
#endif


static CoreClassID CoreTimerID = CORE_CLASS_ID_UNKNOWN;
static CoreClassID CoreRunLoopSourceID = CORE_CLASS_ID_UNKNOWN;
static CoreClassID CoreRunLoopObserverID = CORE_CLASS_ID_UNKNOWN;
static CoreClassID CoreRunLoopModeID = CORE_CLASS_ID_UNKNOWN;
static CoreClassID CoreRunLoopID = CORE_CLASS_ID_UNKNOWN;



#if defined(__WIN32__)
static DWORD __CoreRunLoopThreadKey = ~0;
#elif defined(__LINUX__)
static pthread_key_t __CoreRunLoopThreadKey = ~0;
#endif
 


/*
 *
 *      CORE RUN LOOP MODE
 *
 */

struct __CoreRunLoopMode
{
    CoreRuntimeObject core;
    CoreSpinLock lock; // run loop must be locked before locking this
    CoreImmutableStringRef name;
    CoreSetRef submodes; // real mode objects
    CoreSetRef sources;
    CoreSetRef timers;
    CoreSetRef observers; 
    CoreRunLoopActivity observerMask;   
};



CORE_INLINE void 
__CoreRunLoopMode_lock(CoreRunLoopModeRef rlm)
{
    CoreSpinLock_lock(&rlm->lock);    
}

CORE_INLINE void 
__CoreRunLoopMode_unlock(CoreRunLoopModeRef rlm)
{
    CoreSpinLock_unlock(&rlm->lock);    
}


CORE_INLINE CoreBOOL
__CoreRunLoopMode_isEmpty(CoreRunLoopModeRef rlm)
{
    CoreBOOL result;
    
    result = (
        ((rlm->sources == null) || (CoreSet_getCount(rlm->sources) == 0)) &&
        ((rlm->timers == null) || (CoreSet_getCount(rlm->timers)) == 0)
    ) ? true : false;
    if (!result && (rlm->submodes != null))
    {
        CoreINT_U32 count = CoreSet_getCount(rlm->submodes);
        if (count > 0)
        {
            CoreRunLoopModeRef buffer[32];
            CoreRunLoopModeRef * values;
            CoreINT_U32 idx;
            
            values = (count <= 32) ? buffer : CoreAllocator_allocate(
                Core_getAllocator(rlm),
                count * sizeof(CoreRunLoopModeRef)
            );
            CoreSet_copyValues(rlm->submodes, values);
            for (idx = 0; idx < count; idx++)
            {
                CoreRunLoopModeRef _rlm = values[idx];
                result = (result && __CoreRunLoopMode_isEmpty(_rlm));
            }
            
            if (values != buffer)
            {
                CoreAllocator_deallocate(Core_getAllocator(rlm), values);
            }
        }
    }
    
    return result;
}

static CoreBOOL
__CoreRunLoopMode_equal(CoreObjectRef o1, CoreObjectRef o2)
{
    CoreRunLoopModeRef rlm1 = (CoreRunLoopModeRef) o1;
    CoreRunLoopModeRef rlm2 = (CoreRunLoopModeRef) o2;
    return Core_equal(rlm1->name, rlm2->name);
}

static CoreHashCode
__CoreRunLoopMode_hash(CoreObjectRef o)
{
    CoreRunLoopModeRef rlm = (CoreRunLoopModeRef) o;
    return Core_hash(rlm->name);
}


static const CoreClass __CoreRunLoopModeClass =
{
    0x00,                            // version
    "CoreRunLoopMode",               // name
    NULL,                            // init
    NULL,                            // copy
    NULL,//__CoreRunLoopMode_cleanup,       // cleanup
    __CoreRunLoopMode_equal,         // equal
    __CoreRunLoopMode_hash,          // hash
    NULL,//__CoreRunLoopMode_getCopyOfDescription // getCopyOfDescription
};







/*
 *
 *      CORE TIMER
 *
 */

/*
 * Bits:
 *      ..AB CDEF 
 *      A: isValid
 *      B: isSignaled 
 */       

#define CORE_TIMER_VALID_BIT  0
#define CORE_TIMER_SIGNAL_BIT 1 
   
struct __CoreTimer
{
    CoreRuntimeObject core;
    CoreSpinLock lock;
    CoreRunLoopRef runLoop;
    CoreTimerCallback callback;
    CoreTimerUserInfo userInfo;
    CoreINT_S64 fireTime;
    CoreINT_U32 period;
};


CORE_INLINE void 
__CoreTimer_lock(CoreTimerRef rlt)
{
    CoreSpinLock_lock(&rlt->lock);    
}

CORE_INLINE void 
__CoreTimer_unlock(CoreTimerRef rlt)
{
    CoreSpinLock_unlock(&rlt->lock);    
}

// synchronized
CORE_INLINE void
__CoreTimer_setSignaled(CoreTimerRef rlt, CoreBOOL value)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) rlt)->info, 
        CORE_TIMER_SIGNAL_BIT, 
        1, 
        value
    );
}

// synchronized
CORE_INLINE CoreBOOL
__CoreTimer_isSignaled(CoreTimerRef rlt)
{
    return CoreBitfield_getValue(
        ((const CoreRuntimeObject *) rlt)->info,
        CORE_TIMER_SIGNAL_BIT,
        1
    );
}

// synchronized
CORE_INLINE void
__CoreTimer_setValid(CoreTimerRef rlt, CoreBOOL value)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) rlt)->info, 
        CORE_TIMER_VALID_BIT, 
        1, 
        value
    );
}

// synchronized
CORE_INLINE CoreBOOL
__CoreTimer_isValid(CoreTimerRef rlt)
{
    return CoreBitfield_getValue(
        ((const CoreRuntimeObject *) rlt)->info,
        CORE_TIMER_VALID_BIT,
        1
    );
}

static void
__CoreTimer_cleanup(CoreObjectRef o)
{
    CoreTimerRef rlt = (CoreTimerRef) o;
    
    CORE_DUMP_OBJ_TRACE(o, __FUNCTION__);
    if (rlt->userInfo.release != null)
    {
        rlt->userInfo.release(rlt->userInfo.info);
    }
}

static CoreImmutableStringRef
__CoreTimer_getCopyOfDescription(CoreObjectRef o)
{
    return null;
}

static const CoreClass __CoreTimerClass =
{
    0x00,                            // version
    "CoreTimer",                     // name
    NULL,                            // init
    NULL,                            // copy
    __CoreTimer_cleanup,             // cleanup
    NULL,                            // equal
    NULL,                            // hash
    NULL,//__CoreTimer_getCopyOfDescription // getCopyOfDescription
};


static void 
__CoreTimer_schedule(
    CoreTimerRef rlt,
    CoreRunLoopRef runLoop,
    CoreRunLoopModeRef mode
)
{
    __CoreTimer_lock(rlt);
    rlt->runLoop = Core_retain(runLoop);
    __CoreTimer_unlock(rlt);
}

static void
__CoreTimer_removeFromRunLoop(CoreTimerRef rlt)
{
    CoreRunLoopRef rl = rlt->runLoop;
    CoreImmutableArrayRef modeNames = CoreRunLoop_getCopyOfModes(rl);
    CoreINT_U32 idx, n;
    
    n = CoreArray_getCount(modeNames);
    for (idx = 0; idx < n; idx++)
    {
        CoreImmutableStringRef name = CoreArray_getValueAtIndex(modeNames, idx);
        CoreRunLoop_removeTimer(rl, rlt, name);
    }
    Core_release(modeNames);
}

static void 
__CoreTimer_cancel(
    CoreTimerRef rlt,
    CoreRunLoopRef rl,
    CoreRunLoopModeRef rlm
)
{
    __CoreTimer_lock(rlt);
    if (rlt->runLoop != null)
    {
        rlt->runLoop = null;
    }
    __CoreTimer_unlock(rlt);
}



/* CORE_PUBLIC */ CoreTimerRef 
CoreTimer_create(
    CoreAllocatorRef allocator,
    CoreINT_U32 delay,
    CoreINT_U32 period,
    CoreTimerCallback callback,
    CoreTimerUserInfo * userInfo)
{
    struct __CoreTimer * result = null;
    CoreINT_U32 size = sizeof(struct __CoreTimer);
    
    result = (struct __CoreTimer *) CoreRuntime_createObject(
        allocator, CoreTimerID, size
    );
    if (result != null)
    {
        CoreINT_S64 fireTime;
        CoreINT_S64 now;
        
        now = Core_getCurrentTime_ms();
        fireTime = now + (CoreINT_S64) delay;
        result->fireTime = fireTime;
                
        CoreSpinLock_init(&result->lock);
        __CoreTimer_setValid(result, true);
        result->callback = callback;
        result->runLoop = null; //CoreRunLoop_getCurrent();
        result->period = period;
        if (userInfo != null)
        {
            memcpy(&result->userInfo, userInfo, sizeof(*userInfo));
            if (userInfo->retain != null)
            {
                userInfo->retain(result->userInfo.info);
            }
        }
        else
        {
            memset(&result->userInfo, 0, sizeof(result->userInfo));
        }
    }
    
    return result;
}


/* CORE_PUBLIC */ void 
CoreTimer_cancel(CoreTimerRef rlt)
{
    __CoreTimer_lock(rlt);
    if (__CoreTimer_isValid(rlt))
    {
        CoreRunLoopRef runloop = rlt->runLoop;
        
        __CoreTimer_setValid(rlt, false);
        __CoreTimer_setSignaled(rlt, false);
        __CoreTimer_unlock(rlt);
        
        if (runloop != null)
        {
            __CoreTimer_removeFromRunLoop(rlt);
        }        
    }
    else
    {
        __CoreTimer_unlock(rlt);
    }   
}











/*
 *
 *      CORE RUN LOOP SOURCE
 *
 */
 
/*
 * Bits:
 *      ..AB CDEF 
 *      A: isValid
 *      B: isSignaled 
 */       

#define CORE_RUN_LOOP_SOURCE_VALID_BIT  0
#define CORE_RUN_LOOP_SOURCE_SIGNAL_BIT 1

struct __CoreRunLoopSource
{
    CoreRuntimeObject core;
    CoreSpinLock lock;
    CoreRunLoopRef runLoop;
    CoreINT_S32 priority;
    CoreRunLoopSourceDelegate delegate;
    CoreRunLoopSourceUserInfo userInfo; 
};



CORE_INLINE void 
__CoreRunLoopSource_lock(CoreRunLoopSourceRef rls)
{
    CoreSpinLock_lock(&rls->lock);    
}

CORE_INLINE void 
__CoreRunLoopSource_unlock(CoreRunLoopSourceRef rls)
{
    CoreSpinLock_unlock(&rls->lock);    
}

// synchronized
CORE_INLINE void
__CoreRunLoopSource_setSignaled(CoreRunLoopSourceRef rls, CoreBOOL value)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) rls)->info, 
        CORE_RUN_LOOP_SOURCE_SIGNAL_BIT, 
        1, 
        value
    );
}

// synchronized
CORE_INLINE CoreBOOL
__CoreRunLoopSource_isSignaled(CoreRunLoopSourceRef rls)
{
    return CoreBitfield_getValue(
        ((const CoreRuntimeObject *) rls)->info,
        CORE_RUN_LOOP_SOURCE_SIGNAL_BIT,
        1
    );
}

// synchronized
CORE_INLINE void
__CoreRunLoopSource_setValid(CoreRunLoopSourceRef rls, CoreBOOL value)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) rls)->info, 
        CORE_RUN_LOOP_SOURCE_VALID_BIT, 
        1, 
        value
    );
}

// synchronized
CORE_INLINE CoreBOOL
__CoreRunLoopSource_isValid(CoreRunLoopSourceRef rls)
{
    return CoreBitfield_getValue(
        ((const CoreRuntimeObject *) rls)->info,
        CORE_RUN_LOOP_SOURCE_VALID_BIT,
        1
    );
}


static CoreBOOL
__CoreRunLoopSource_equal(CoreObjectRef o1, CoreObjectRef o2)
{
    CoreBOOL result = true;
    CoreRunLoopSourceRef rls1 = (CoreRunLoopSourceRef) o1;
    CoreRunLoopSourceRef rls2 = (CoreRunLoopSourceRef) o2;
    
    if (rls1 != rls2)
    {
        // this section should be more comprehensive
        result = (rls1->userInfo.info == rls2->userInfo.info);    
    }
    
    return result;
}

static CoreHashCode
__CoreRunLoopSource_hash(CoreObjectRef o)
{
    CoreHashCode result = 0;
    CoreRunLoopSourceRef rls = (CoreRunLoopSourceRef) o;
    
    CORE_DUMP_OBJ_TRACE(o, __FUNCTION__);
    if (rls->userInfo.hash != null)
    {
        result = rls->userInfo.hash(rls->userInfo.info);
    }
    else
    {
        result = (CoreHashCode) rls->userInfo.info;
    }
    
    return result;
}

static void
__CoreRunLoopSource_cleanup(CoreObjectRef o)
{
    CoreRunLoopSourceRef rls = (CoreRunLoopSourceRef) o;
    
    CORE_DUMP_OBJ_TRACE(o, __FUNCTION__);
    if (rls->userInfo.release != null)
    {
        rls->userInfo.release(rls->userInfo.info);
    }
}


static const CoreClass __CoreRunLoopSourceClass =
{
    0x00,                            // version
    "CoreRunLoopSource",             // name
    NULL,                            // init
    NULL,                            // copy
    __CoreRunLoopSource_cleanup,     // cleanup
    __CoreRunLoopSource_equal,       // equal
    __CoreRunLoopSource_hash,        // hash
    NULL,//__CoreRunLoopSource_getCopyOfDescription // getCopyOfDescription
};


/* CORE_PROTECTED */ CoreRunLoopSourceRef
CoreRunLoopSource_createWithPriority(
    CoreAllocatorRef allocator,
    CoreRunLoopSourceDelegate * delegate,
    CoreRunLoopSourceUserInfo * userInfo,
    CoreINT_S32 priority)
{
    struct __CoreRunLoopSource * result = null;
    CoreINT_U32 size;
    
    if (delegate != null)
    {
        size = sizeof(struct __CoreRunLoopSource);
        result = (struct __CoreRunLoopSource *) CoreRuntime_createObject(
            allocator, CoreRunLoopSourceID, size
        );
        if (result != null)
        {
            CoreSpinLock_init(&result->lock);
            result->priority = 0;
            result->runLoop = null;
            __CoreRunLoopSource_setValid(result, true);
            memcpy(&result->delegate, delegate, sizeof(*delegate));
            if (userInfo != null)
            {
                memcpy(&result->userInfo, userInfo, sizeof(*userInfo));
                if (userInfo->retain != null)
                {
                    userInfo->retain(result->userInfo.info);
                }
            }
            else
            {
                memset(&result->userInfo, 0, sizeof(result->userInfo));
            }
        }
    }
    
    return result;
}

/* CORE_PUBLIC */ CoreRunLoopSourceRef
CoreRunLoopSource_create(
    CoreAllocatorRef allocator,
    CoreRunLoopSourceDelegate * delegate,
    CoreRunLoopSourceUserInfo * userInfo)
{
    CoreRunLoopSourceRef result;
    
    result = CoreRunLoopSource_createWithPriority(
        allocator, delegate, userInfo, 0
    );
    CORE_DUMP_MSG(
        CORE_LOG_TRACE | CORE_LOG_INFO, 
        "->%s: new object %p\n", __FUNCTION__, result
    );
    
    return result;        
}


/* CORE_PUBLIC */ void 
CoreRunLoopSource_signal(CoreRunLoopSourceRef rls)
{
    CORE_DUMP_OBJ_TRACE(rls, __FUNCTION__);
    
    __CoreRunLoopSource_lock(rls);
    if (__CoreRunLoopSource_isValid(rls))
    {
        __CoreRunLoopSource_setSignaled(rls, true);
    }
    __CoreRunLoopSource_unlock(rls);    
}

/* CORE_PUBLIC */ CoreBOOL 
CoreRunLoopSource_isValid(CoreRunLoopSourceRef rls)
{
    CoreBOOL result = false;
    
    result = __CoreRunLoopSource_isValid(rls);
   
    return result;
}

/* CORE_PUBLIC */ CoreRunLoopRef 
CoreRunLoopSource_getRunLoop(CoreRunLoopSourceRef rls)
{
    CoreRunLoopRef result;
    
    __CoreRunLoopSource_lock(rls);
    result = rls->runLoop;
    __CoreRunLoopSource_unlock(rls);
    
    return result;
}


static void
__CoreRunLoopSource_removeFromRunLoop(CoreRunLoopSourceRef rls)
{
    CoreRunLoopRef rl = rls->runLoop;
    CoreImmutableArrayRef modeNames = CoreRunLoop_getCopyOfModes(rl);
    CoreINT_U32 idx, n;
    
    n = CoreArray_getCount(modeNames);
    for (idx = 0; idx < n; idx++)
    {
        CoreImmutableStringRef name = CoreArray_getValueAtIndex(modeNames, idx);
        CoreRunLoop_removeSource(rl, rls, name);
    }
    Core_release(modeNames);
}


static void 
__CoreRunLoopSource_schedule(
    CoreRunLoopSourceRef rls,
    CoreRunLoopRef runLoop,
    CoreRunLoopModeRef mode
)
{
    __CoreRunLoopSource_lock(rls);
    rls->runLoop = runLoop;
    __CoreRunLoopSource_unlock(rls);
    if (rls->delegate.schedule != null)
    {
        // notify
        rls->delegate.schedule(rls->userInfo.info, runLoop, mode->name);
    }
}

static void 
__CoreRunLoopSource_cancel(
    CoreRunLoopSourceRef rls,
    CoreRunLoopRef rl,
    CoreRunLoopModeRef rlm
)
{
    if (rls->delegate.cancel != null)
    {
        // notify
        rls->delegate.cancel(rls->userInfo.info, rl, rlm->name);
    }
    __CoreRunLoopSource_lock(rls);
    if (rls->runLoop != null)
    {
        rls->runLoop = null;
    }
    __CoreRunLoopSource_unlock(rls);
}


/* CORE_PUBLIC */ void 
CoreRunLoopSource_cancel(CoreRunLoopSourceRef rls)
{
    Core_retain(rls);
    __CoreRunLoopSource_lock(rls);
    if (__CoreRunLoopSource_isValid(rls))
    {
        CoreRunLoopRef runloop = rls->runLoop;
        
        __CoreRunLoopSource_setValid(rls, false);
        __CoreRunLoopSource_setSignaled(rls, false);
        __CoreRunLoopSource_unlock(rls);
        
        if (runloop != null)
        {
            __CoreRunLoopSource_removeFromRunLoop(rls);
        }
    }
    else
    {
        __CoreRunLoopSource_unlock(rls);
    }
    Core_release(rls);        
}

/* CORE_PROTECTED */ CoreINT_S32 
CoreRunLoopSource_getPriority(CoreRunLoopSourceRef rls)
{
    return rls->priority;
}








/*
 *
 *      CORE RUN LOOP OBSERVER
 *
 */

#define CORE_RUN_LOOP_OBSERVER_VALID_BIT  0
#define CORE_RUN_LOOP_OBSERVER_SIGNAL_BIT 1

struct __CoreRunLoopObserver
{
    CoreRuntimeObject core;
    CoreSpinLock lock;
    CoreRunLoopRef runLoop;
    CoreRunLoopActivity activities;
    CoreINT_S32 priority;
    CoreRunLoopObserverCallback callback;
    CoreRunLoopObserverUserInfo userInfo;    
};



CORE_INLINE void 
__CoreRunLoopObserver_lock(CoreRunLoopObserverRef rlo)
{
    CoreSpinLock_lock(&rlo->lock);    
}

CORE_INLINE void 
__CoreRunLoopObserver_unlock(CoreRunLoopObserverRef rlo)
{
    CoreSpinLock_unlock(&rlo->lock);    
}

// synchronized
CORE_INLINE void
__CoreRunLoopObserver_setSignaled(CoreRunLoopObserverRef rlo, CoreBOOL value)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) rlo)->info, 
        CORE_RUN_LOOP_OBSERVER_SIGNAL_BIT, 
        1, 
        value
    );
}

// synchronized
CORE_INLINE CoreBOOL
__CoreRunLoopObserver_isSignaled(CoreRunLoopObserverRef rlo)
{
    return CoreBitfield_getValue(
        ((const CoreRuntimeObject *) rlo)->info,
        CORE_RUN_LOOP_OBSERVER_SIGNAL_BIT,
        1
    );
}

// synchronized
CORE_INLINE void
__CoreRunLoopObserver_setValid(CoreRunLoopObserverRef rlo, CoreBOOL value)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) rlo)->info, 
        CORE_RUN_LOOP_OBSERVER_VALID_BIT, 
        1, 
        value
    );
}

// synchronized
CORE_INLINE CoreBOOL
__CoreRunLoopObserver_isValid(CoreRunLoopObserverRef rlo)
{
    return CoreBitfield_getValue(
        ((const CoreRuntimeObject *) rlo)->info,
        CORE_RUN_LOOP_OBSERVER_VALID_BIT,
        1
    );
}


CORE_INLINE void
__CoreRunLoopObserver_schedule(
    CoreRunLoopObserverRef rlo,
    CoreRunLoopRef rl,
    CoreRunLoopModeRef rlm
)
{
    __CoreRunLoopObserver_lock(rlo);
    rlo->runLoop = rl;
    __CoreRunLoopObserver_setValid(rlo, true);
    __CoreRunLoopObserver_unlock(rlo);
}

static void
__CoreRunLoopObserver_removeFromRunLoop(CoreRunLoopObserverRef rlo)
{
    CoreRunLoopRef rl = rlo->runLoop;
    CoreImmutableArrayRef modeNames = CoreRunLoop_getCopyOfModes(rl);
    CoreINT_U32 idx, n;
    
    n = CoreArray_getCount(modeNames);
    for (idx = 0; idx < n; idx++)
    {
        CoreImmutableStringRef name = CoreArray_getValueAtIndex(modeNames, idx);
        CoreRunLoop_removeObserver(rl, rlo, name);
    }
    Core_release(modeNames);
}

static void 
__CoreRunLoopObserver_cancel(
    CoreRunLoopObserverRef rlo
)
{
    __CoreRunLoopObserver_lock(rlo);
    if (rlo->runLoop != null)
    {
        rlo->runLoop = null;
    }
    __CoreRunLoopObserver_unlock(rlo);
}


static const CoreClass __CoreRunLoopObserverClass =
{
    0x00,                            // version
    "CoreRunLoopObserver",               // name
    NULL,                            // init
    NULL,                            // copy
    NULL,//__CoreRunLoopObserver_cleanup,       // cleanup
    NULL,                            // equal
    NULL,                            // hash
    NULL,//__CoreRunLoopObserver_getCopyOfDescription // getCopyOfDescription
};



/* CORE_PUBLIC */ CoreRunLoopObserverRef 
CoreRunLoopObserver_create(
    CoreAllocatorRef allocator,
    CoreRunLoopActivity activities,
    CoreRunLoopObserverCallback callback,
    CoreRunLoopObserverUserInfo * userInfo)
{
    struct __CoreRunLoopObserver * result = null;
    CoreINT_U32 size = sizeof(struct __CoreRunLoopObserver);
    
    result = (struct __CoreRunLoopObserver *) CoreRuntime_createObject(
        allocator, CoreRunLoopObserverID, size
    );
    if (result != null)
    {
        CoreSpinLock_init(&result->lock);
        result->callback = callback;
        result->runLoop = null;
        result->activities = activities;
        result->priority = 0;
        if (userInfo != null)
        {
            memcpy(&result->userInfo, userInfo, sizeof(*userInfo));
            if (userInfo->retain != null)
            {
                userInfo->retain(result->userInfo.info);
            }
        }
        else
        {
            memset(&result->userInfo, 0, sizeof(result->userInfo));
        }
    }
    
    return result;
}

/* CORE_PUBLIC */ CoreBOOL 
CoreRunLoopObserver_isValid(CoreRunLoopObserverRef rlo)
{
    CoreBOOL result = false;
    
    __CoreRunLoopObserver_lock(rlo); 
    result = __CoreRunLoopObserver_isValid(rlo);
    __CoreRunLoopObserver_unlock(rlo);
    
    return result;
}


/* CORE_PUBLIC */ void 
CoreRunLoopObserver_cancel(CoreRunLoopObserverRef rlo)
{
    Core_retain(rlo);
    __CoreRunLoopObserver_lock(rlo);
    if (__CoreRunLoopObserver_isValid(rlo))
    {
        CoreRunLoopRef rl = rlo->runLoop;
        __CoreRunLoopObserver_setValid(rlo, false);
        __CoreRunLoopObserver_unlock(rlo);
        if (rl != null)
        {
            __CoreRunLoopObserver_removeFromRunLoop(rlo);    
        }
    }
    else
    {
        __CoreRunLoopObserver_unlock(rlo);
    }    
    Core_release(rlo);    
}















/*
 *
 *      CORE RUN LOOP
 *
 */

#define CORE_RUN_LOOP_SLEEPING_BIT  1
#define CORE_RUN_LOOP_CLEANING_BIT  2
#define CORE_RUN_LOOP_STOP_BIT      3

/*
#define CORE_RUN_LOOP_STATE_START   1
#define CORE_RUN_LOOP_STATE_LENGTH  2
#define CORE_RUN_LOOP_STATE_STOP        0
#define CORE_RUN_LOOP_STATE_SLEEPING    1
#define CORE_RUN_LOOP_STATE_CLEANING    2  
*/

#if defined(__LINUX__)
typedef int SOCKET;
#endif
  
struct __CoreRunLoop
{
    CoreRuntimeObject core;
    CoreSpinLock lock;  // for accesing modes; never try to lock with mode locked
    CoreSetRef modes;   // modes' names only
    CoreRunLoopModeRef currentMode;
    CoreINT_S64 wakeupTime;
#if defined(__LINUX__)
    SOCKET selfpipe[2];
#elif defined(__WIN32__)
    HANDLE port;
#endif        
};



CORE_INLINE void __CoreRunLoop_lock(CoreRunLoopRef rl)
{
    CoreSpinLock_lock(&rl->lock);    
}

CORE_INLINE void __CoreRunLoop_unlock(CoreRunLoopRef rl)
{
    CoreSpinLock_unlock(&rl->lock);    
}

// synchronized
CORE_INLINE CoreBOOL
__CoreRunLoop_isStopped(CoreRunLoopRef rl)
{
    return CoreBitfield_getValue(
        ((CoreRuntimeObject *) rl)->info, 
        CORE_RUN_LOOP_STOP_BIT, 
        1
    );
}

CORE_INLINE void
__CoreRunLoop_setStopped(CoreRunLoopRef rl, CoreBOOL newValue)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) rl)->info, 
        CORE_RUN_LOOP_STOP_BIT, 
        1, 
        newValue
    );
}

CORE_INLINE CoreBOOL
__CoreRunLoop_isSleeping(CoreRunLoopRef rl)
{
    return CoreBitfield_getValue(
        ((const CoreRuntimeObject *) rl)->info,
        CORE_RUN_LOOP_SLEEPING_BIT,
        1
    );
}

CORE_INLINE void
__CoreRunLoop_setSleeping(CoreRunLoopRef rl, CoreBOOL newValue)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) rl)->info, 
        CORE_RUN_LOOP_SLEEPING_BIT, 
        1, 
        newValue
    );
}

CORE_INLINE CoreBOOL
__CoreRunLoop_isDeallocating(CoreRunLoopRef rl)
{
    return CoreBitfield_getValue(
        ((const CoreRuntimeObject *) rl)->info,
        CORE_RUN_LOOP_CLEANING_BIT,
        1
    );
}

CORE_INLINE void
__CoreRunLoop_setDeallocating(CoreRunLoopRef rl, CoreBOOL newValue)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) rl)->info, 
        CORE_RUN_LOOP_CLEANING_BIT, 
        1, 
        newValue
    );
}


/*
 * Tries to find a mode according to its name. If found, returns a reference 
 *  to it. Otherwise if 'create' is true, creates it and returns.
 */    
/* run-loop is locked */
static CoreRunLoopModeRef 
__CoreRunLoop_findMode(
    CoreRunLoopRef rl,
    CoreImmutableStringRef modeName,
    CoreBOOL create
)
{
    CoreRunLoopModeRef result = null;
    struct __CoreRunLoopMode tmp;
    
    CoreRuntime_initStaticObject(&tmp, CoreRunLoopModeID);
    tmp.name = modeName;
    result = (CoreRunLoopModeRef) CoreSet_getValue(rl->modes, (const void *) &tmp);
    if ((result == null) && create)
    {
        result = (CoreRunLoopModeRef) CoreRuntime_createObject(
            Core_getAllocator(rl),
            CoreRunLoopModeID,
            sizeof(struct __CoreRunLoopMode)
        );
        if (result != null)
        {
            result->sources = null;
            result->timers = null;
            result->observers = null;
            result->submodes = null;
            result->observerMask = 0;
            CoreSpinLock_init(&result->lock);
            result->name = Core_retain(modeName);
            CoreSet_addValue(rl->modes, result);
        }
    }
    
    return result;    
}


#if defined(__LINUX__)
static void 
__CoreRunLoop_wakeUp(CoreRunLoopRef rl)
{
    char c = 'w';
    (void) send(rl->selfpipe[0], &c, sizeof(c), 0);    
}
#elif defined(__WIN32__)
static void 
__CoreRunLoop_wakeUp(CoreRunLoopRef rl)
{
    /*fprintf(stdout, "rl %p triggering wake up\n", rl);
    fflush(stdout);*/
    SetEvent(rl->port);
}
#endif


static void 
__CoreRunLoop_findMinTimer(const void * value, void * context)
{
    CoreTimerRef timer = (CoreTimerRef) value;
    
    if (__CoreTimer_isValid(timer))
    {
        CoreTimerRef * result = context;

        if ((*result == null) || (timer->fireTime < (*result)->fireTime))
        {
            *result = timer;
        }
    }
}

static CoreINT_S64 
__CoreRunLoop_getMinFireTime(CoreRunLoopRef rl, CoreRunLoopModeRef rlm)
{
    CoreINT_S64 result = -1;
    
    if ((rlm->timers != null) && (CoreSet_getCount(rlm->timers) > 0))
    {
        CoreTimerRef minTimer = null;
        
        CoreSet_applyFunction(
            rlm->timers, 
            __CoreRunLoop_findMinTimer,
            &minTimer
        );
        if (minTimer != null)
        {
            result = minTimer->fireTime;
        }        
    }
    
    return result;
}

#if defined(__LINUX__)
static CoreRunLoopSleepResult 
__CoreRunLoop_sleep(
    CoreRunLoopRef rl, 
    CoreRunLoopModeRef rlm, 
    CoreINT_S64 timeout)
{
    CoreRunLoopSleepResult result = 0;
    int res;
    struct pollfd fds[1];
    CoreINT_S64 sleepTime = 0;
    CoreINT_S64 fireTime;
    
    __CoreRunLoop_lock(rl);
    __CoreRunLoopMode_lock(rlm);
    __CoreRunLoop_unlock(rl);
    fireTime = __CoreRunLoop_getMinFireTime(rl, rlm);
    __CoreRunLoopMode_unlock(rlm);

    //
    // Compute sleep time...
    //
    
    // when zero timeout, set the result, however continue in computing --
    // fired timers can override this result
    if (timeout == 0)
    {
        result = CORE_RUN_LOOP_SLEEP_TIMED_OUT;
    }
    
    if (fireTime >= 0)
    {
        CoreINT_S64 now_ms = Core_getCurrentTime_ms();

        if (fireTime <= now_ms)
        {
            result = CORE_RUN_LOOP_SLEEP_TIMEOUT_FIRED; // sleepTime remains 0
        }
        else
        {
            sleepTime = fireTime - now_ms;
            sleepTime = min(sleepTime, timeout);
        }
    }
    else
    {
        sleepTime = timeout;
    }
    
    if (sleepTime > 0)
    {
        fds[0].fd = rl->selfpipe[1];
        fds[0].events = POLLIN;
        
        CORE_DUMP_MSG(
            CORE_LOG_INFO, 
            "RunLoop %p going to sleep for %ums\n", rl, (CoreINT_U32) sleepTime
        );
        
        // go to sleep
        do
        {
            res = poll(fds, 1, (CoreINT_S32) sleepTime);
        }    
        while (res == -EINTR);
    
        CORE_DUMP_MSG(CORE_LOG_INFO, "RunLoop %p woken up with res %d\n", rl);
         
        //
        // Evaluation...
        //  == 0: timeout expired
        //   > 0: externally woken up
        //   < 0: error (??)
        //
        if (res > 0)
        {
            result = CORE_RUN_LOOP_SLEEP_WOKEN_UP;
            if (fds[0].revents & POLLIN)
            {
                int cnt;
                do
                {
                    char buff[64] = {0}; // actually, there might be more
                                         // characters in the pipe
                    cnt = recv(rl->selfpipe[1], buff, 64, 0);
                }
                while (cnt > 0);
            }
        }
        else if (res == 0)
        {
            if (sleepTime == timeout)
            {
                result = CORE_RUN_LOOP_SLEEP_TIMED_OUT;
            }
            else
            {
                result = CORE_RUN_LOOP_SLEEP_TIMEOUT_FIRED;
            }
        }
        else 
        {
            result = CORE_RUN_LOOP_SLEEP_ERROR;
        }
    }
    
    return result;
}

#elif defined(__WIN32__)
static CoreRunLoopSleepResult 
__CoreRunLoop_sleep(
    CoreRunLoopRef rl, 
    CoreRunLoopModeRef rlm, 
    CoreINT_S64 timeout)
{
    CoreRunLoopSleepResult result = 0;
    CoreINT_S64 sleepTime = 0;
    CoreINT_S64 fireTime;
    DWORD res;
    
    __CoreRunLoop_lock(rl);
    __CoreRunLoopMode_lock(rlm);
    __CoreRunLoop_unlock(rl);
    fireTime = __CoreRunLoop_getMinFireTime(rl, rlm);
    __CoreRunLoopMode_unlock(rlm);

    //
    // Compute sleep time...
    //

    // when zero timeout, set the result, however continue in computing --
    // fired timers can override this result
    if (timeout == 0)
    {
        result = CORE_RUN_LOOP_SLEEP_TIMED_OUT;
    }
    
    if (fireTime >= 0)
    {
        CoreINT_S64 now = (CoreINT_S64) Core_getCurrentTime_ms();
        sleepTime = fireTime - now;
        sleepTime = min(sleepTime, timeout);
        if (fireTime <= now)
        {
            result = CORE_RUN_LOOP_SLEEP_TIMEOUT_FIRED;
        }
        else
        {
            sleepTime = fireTime - now;
            sleepTime = min(sleepTime, timeout);
        }
    }
    else
    {
        sleepTime = timeout;
    }
    
    if (sleepTime > 0)
    {
        CORE_DUMP_MSG(
            CORE_LOG_INFO, 
            "RunLoop %p going to sleep for %ums\n", rl, (CoreINT_U32) sleepTime
        );

        res = WaitForSingleObject(rl->port, (CoreINT_S32) sleepTime);

        CORE_DUMP_MSG(CORE_LOG_INFO, "RunLoop %p woken up\n", rl);
        
        //
        // Evaluation...
        //  == WAIT_OBJECT_0: externally woken up
        //  == WAIT_TIMEOUT: timeout expired
        //  otherwise: error (??)
        //
        if (res == WAIT_OBJECT_0)
        {
            result = CORE_RUN_LOOP_SLEEP_WOKEN_UP;
        }
        else if (res == WAIT_TIMEOUT)
        {
            if (sleepTime == timeout)
            {
                result = CORE_RUN_LOOP_SLEEP_TIMED_OUT;
            }
            else
            {
                result = CORE_RUN_LOOP_SLEEP_TIMEOUT_FIRED;
            }
        }
        else
        {
            result = CORE_RUN_LOOP_SLEEP_ERROR;
        }
    }   
    
    return result; 
}

#endif

static CoreRunLoopSleepResult 
__CoreRunLoop_wait(
    CoreRunLoopRef rl, 
    CoreRunLoopModeRef rlm, 
    CoreINT_S64 timeout)
{
    CoreRunLoopSleepResult result;
    
    __CoreRunLoop_setSleeping(rl, true);
    result = __CoreRunLoop_sleep(rl, rlm, timeout);
    __CoreRunLoop_setSleeping(rl, false);
    
    return result;
}


struct CollectObserversContext
{           
    CoreRunLoopRef _rl;
    CoreRunLoopActivity _activity;
    CoreINT_U32 * _count;
    CoreObjectRef * _observers;
};

static __CoreRunLoop_collectObservers(
    CoreRunLoopModeRef rlm,
    void * context
)
{
    struct CollectObserversContext * collect = 
        (struct CollectObserversContext *) context;
    CoreINT_U32 * idx = collect->_count;
    __CoreIteratorState state = { 0 };
    const void * items[8];
    CoreINT_U32 limit;
    
            
    limit = _CoreSet_iterate(rlm->observers, &state, items, 8);
    while (limit > 0) 
    {
        CoreINT_U32 counter = 0;
        do
        {
            CoreRunLoopObserverRef observer = state.items[counter++];
            if ((observer->activities & collect->_activity)
                && __CoreRunLoopObserver_isValid(observer))
            {
                collect->_observers[(*idx)++] = Core_retain(observer);
            }
        } 
        while (counter < limit);
        limit = _CoreSet_iterate(rlm->observers, &state, items, 8);
    }
}

// mode (aka value) is locked on entry
static void __CoreRunLoop_collectObserversInMode(
    const void * value,
    void * context
)
{
    CoreRunLoopModeRef rlm = (CoreRunLoopModeRef) value;
    
    // Note that I don't lock the mode here... all locked modes were already
    // locked in __CoreRunLoop_getCountOfObserversInMode().
    if ((rlm->observers != null) && (CoreSet_getCount(rlm->observers) > 0))
    {
        __CoreRunLoop_collectObservers(rlm, context);
    }
    if ((rlm->submodes != null) && (CoreSet_getCount(rlm->submodes) > 0))
    {
        CoreSet_applyFunction(
            rlm->submodes, __CoreRunLoop_collectObserversInMode, context
        );   
    }
    __CoreRunLoopMode_unlock(rlm);
}

static CoreComparison
__CoreRunLoopObserverComparator(const void * v1, const void * v2)
{
    CoreComparison result;
    CoreRunLoopObserverRef o1 = (CoreRunLoopObserverRef) v1;
    CoreRunLoopObserverRef o2 = (CoreRunLoopObserverRef) v2;
    
    if (o1->priority < o2->priority)
    {
        result = CORE_COMPARISON_LESS_THAN;
    }
    else if (o1->priority > o2->priority)
    {
        result = CORE_COMPARISON_GREATER_THAN;
    }
    else
    {
        result = CORE_COMPARISON_EQUAL;
    }
    
    return result;        
}

static void __CoreRunLoop_getCountOfObserversInMode(
    const void * value,
    void * context
)
{
    CoreRunLoopModeRef rlm = (CoreRunLoopModeRef) value;
    CoreINT_U32 * count = (CoreINT_U32 *) context;
        
    // Note that I don't unlock the mode here... all locked modes are unlocked
    // in __CoreRunLoop_collectObserversInMode().
    __CoreRunLoopMode_lock(rlm);
    if (rlm->observers != null)
    {
        *count = *count + CoreSet_getCount(rlm->observers);
    }
    if ((rlm->submodes != null) && (CoreSet_getCount(rlm->submodes) > 0))
    {
        CoreSet_applyFunction(
            rlm->submodes, __CoreRunLoop_getCountOfObserversInMode, context
        );   
    }
}

// mode is locked on entry and exit
static void 
__CoreRunLoop_doObservers(
    CoreRunLoopRef rl, 
    CoreRunLoopModeRef rlm, 
    CoreRunLoopActivity activity
)
{
    CoreINT_U32 idx;
    CoreObjectRef buffer[32];
    CoreINT_U32 count = 0;
    CoreINT_U32 activeCount = 0;
    CoreObjectRef * observers = buffer;
    struct CollectObserversContext context = { rl, activity, &activeCount, observers };
    
    // Find out how many active observers we have for the activity and
    // according to it, allocate correspondent buffer.
    if (rlm->observers != null)
    {
        count = CoreSet_getCount(rlm->observers);
    }
    if ((rlm->submodes != null) && (CoreSet_getCount(rlm->submodes) > 0))
    {
        CoreSet_applyFunction(
            rlm->submodes, __CoreRunLoop_getCountOfObserversInMode, &count
        );
    }
    if (count > 32)
    {
        observers = CoreAllocator_allocate(
            CORE_ALLOCATOR_SYSTEM, count * sizeof(CoreRunLoopObserverRef)
        );
    }

    // Then collect these observers from all submodes.
    if ((rlm->observers != null) && (CoreSet_getCount(rlm->observers) > 0))
    {
        __CoreRunLoop_collectObservers(rlm, &context);
    }
    if ((rlm->submodes != null) && (CoreSet_getCount(rlm->submodes) > 0))
    {
        CoreSet_applyFunction(
            rlm->submodes, __CoreRunLoop_collectObserversInMode, &context
        );
    }
    
    // If we collect at least one observer, unlock the mode for following
    // callback's callout.
    if (count > 0)
    {
        __CoreRunLoopMode_unlock(rlm);
    }
    for (idx = 0; idx < count; idx++)
    {
        CoreRunLoopObserverRef observer = (CoreRunLoopObserverRef) context._observers[idx];
        if (__CoreRunLoopObserver_isValid(observer))
        {
            observer->callback(observer, activity, observer->userInfo.info);
        }
        Core_release(observer);
    }
    
    if (context._observers != buffer)
    {
        CoreAllocator_deallocate(CORE_ALLOCATOR_SYSTEM, context._observers);
    }
    __CoreRunLoopMode_lock(rlm);
}   


static CoreComparison
__CoreTimersComparator(const void * v1, const void * v2)
{
    CoreComparison result;
    CoreTimerRef tm1 = (CoreTimerRef) v1;
    CoreTimerRef tm2 = (CoreTimerRef) v2;
    
    if (tm1->fireTime < tm2->fireTime)
    {
        result = CORE_COMPARISON_LESS_THAN;
    }
    else if (tm1->fireTime > tm2->fireTime)
    {
        result = CORE_COMPARISON_GREATER_THAN;
    }
    else
    {
        result = CORE_COMPARISON_EQUAL;
    }
    
    return result;        
}



struct CollectTimersContext
{           
    CoreObjectRef * timers;
    CoreINT_S64 now;
};

// Collects all timers that were already signalled.
static void 
__CoreRunLoop_collectTimers(
    const void * value,
    void * context
)
{
    CoreTimerRef timer = (CoreTimerRef) value;
    struct CollectTimersContext * collect = (struct CollectTimersContext *) context;
    CoreObjectRef * timers = collect->timers;
    
    if (__CoreTimer_isValid(timer) && (timer->fireTime <= collect->now))
    {
        if (*timers == null)
        {
            // this is the first found source
            *timers = Core_retain(timer);
        }
        else if (Core_getClassID(*timers) == CoreTimerID)
        {
            // the second one was found, need to create an array
            CoreArrayRef array = CoreArray_create(
                CORE_ALLOCATOR_SYSTEM, 0, &CoreArrayCoreCallbacks);
            if (array != null)
            {
                CoreArray_addValue(array, *timers);
                CoreArray_addValue(array, timer);
                Core_release(*timers);
                *timers = array;
            }
        }
        else
        {
            // an array was already created, so just add another item to it
            CoreArray_addValue((CoreArrayRef) *timers, timer);
        }
    }
}

static void __CoreRunLoop_collectTimersInMode(
    const void * value, void * context
)
{
    CoreRunLoopModeRef rlm = (CoreRunLoopModeRef) value;
    
    __CoreRunLoopMode_lock(rlm);
    if ((rlm->timers != null) && (CoreSet_getCount(rlm->timers) > 0))
    {
        CoreSet_applyFunction(
            rlm->timers, __CoreRunLoop_collectTimers, context
        );
    }
    if ((rlm->submodes != null) && (CoreSet_getCount(rlm->submodes) > 0))
    {
        CoreSet_applyFunction(
            rlm->submodes, __CoreRunLoop_collectTimersInMode, context
        );   
    }
    __CoreRunLoopMode_unlock(rlm);
}

static void __CoreRunLoop_doTimer(
    CoreTimerRef rt, CoreRunLoopRef rl, CoreRunLoopModeRef rlm, CoreINT_S64 now
)
{
     __CoreTimer_lock(rt);
     __CoreTimer_setSignaled(rt, false);
     if (__CoreTimer_isValid(rt))
     {
         __CoreTimer_unlock(rt);
         if (rt->callback != null)
         {
             rt->callback(rt, rt->userInfo.info); // callout
         }
         
         // check periodicity... possible rescheduling
         if (rt->period == 0)
         {
             CoreTimer_cancel(rt);
         }
         else
         {
             // Now the question -- should we set the next fireTime according
             // to scheduled fire time or according to actual fired time?
             
             //rt->fireTime = now + (CoreINT_S64) rt->period; // actual
             while (rt->fireTime <= now) // scheduled
             {
                rt->fireTime += (CoreINT_S64) rt->period;
             }
         }
     }
     else
     {
         __CoreTimer_unlock(rt);
     }
}

/* performs all pending timers for given mode */
/* mode is locked on entry and exit */
static CoreBOOL __CoreRunLoop_doTimers(
    CoreRunLoopRef rl,
    CoreRunLoopModeRef rlm
)
{
    CoreBOOL result = false;
    CoreObjectRef timers = null;
    CoreINT_S64 now = Core_getCurrentTime_ms();
    struct CollectTimersContext context = { &timers, now };
    
    if ((rlm->timers != null) && (CoreSet_getCount(rlm->timers) > 0))
    {
        CoreSet_applyFunction(
            rlm->timers, __CoreRunLoop_collectTimers, &context
        );
    }

    // now get timers from all submodes
    if ((rlm->submodes != null) && (CoreSet_getCount(rlm->submodes) > 0))
    {
        CoreSet_applyFunction(
            rlm->submodes, __CoreRunLoop_collectTimersInMode, &context
        );
    }
    
    if (timers != null)
    {
        __CoreRunLoopMode_unlock(rlm);
        if (Core_getClassID(timers) == CoreTimerID)
        {
            CoreTimerRef timer = (CoreTimerRef) timers;
            __CoreRunLoop_doTimer(timer, rl, rlm, now);
        }
        else
        {
            CoreArrayRef array = (CoreArrayRef) timers;
            CoreINT_U32 idx, n;
            
            n = CoreArray_getCount(array);
            CoreArray_sortValues(
                array,
                CoreRange_create(0, n),
                __CoreTimersComparator    
            );
            
            for (idx = 0; idx < n; idx++)
            {
                CoreTimerRef timer;
                
                timer = (CoreTimerRef) CoreArray_getValueAtIndex(array, idx);
                __CoreRunLoop_doTimer(timer, rl, rlm, now);
            }
        }
        Core_release(timers);
        __CoreRunLoopMode_lock(rlm);
    }
    
    return result;    
}    


static CoreComparison
__CoreRunLoopSourceComparator(const void * v1, const void * v2)
{
    CoreComparison result;
    CoreRunLoopSourceRef src1 = (CoreRunLoopSourceRef) v1;
    CoreRunLoopSourceRef src2 = (CoreRunLoopSourceRef) v2;
    
    if (src1->priority < src2->priority)
    {
        result = CORE_COMPARISON_LESS_THAN;
    }
    else if (src1->priority > src2->priority)
    {
        result = CORE_COMPARISON_GREATER_THAN;
    }
    else
    {
        result = CORE_COMPARISON_EQUAL;
    }
    
    return result;        
}

// Collects all sources that were already signalled.
static void __CoreRunLoop_collectSources(
    const void * value,
    void * context
)
{
    CoreRunLoopSourceRef src = (CoreRunLoopSourceRef) value;
    CoreObjectRef * sources = (CoreObjectRef *) context;
    
    if (__CoreRunLoopSource_isValid(src) && __CoreRunLoopSource_isSignaled(src))
    {
        if (*sources == null)
        {
            // this is the first found source
            *sources = Core_retain(src);
        }
        else if (Core_getClassID(*sources) == CoreRunLoopSourceID)
        {
            // the second one was found, need to create an array
            CoreArrayRef array = CoreArray_create(
                CORE_ALLOCATOR_SYSTEM, 0, &CoreArrayCoreCallbacks
            );
            if (array != null)
            {
                CoreArray_addValue(array, *sources);
                CoreArray_addValue(array, src);
                Core_release(*sources);
                *sources = array;
            }
        }
        else
        {
            // an array was already created, so just add another item to it
            CoreArray_addValue((CoreArrayRef) *sources, src);
        }
    }
}

static __CoreRunLoop_collectSources_new(
    CoreRunLoopModeRef rlm,
    void * context
)
{
    CoreObjectRef * sources = (CoreObjectRef *) context;    
    __CoreIteratorState state = { 0 };
    const void * items[8];
    CoreINT_U32 limit;
    CoreINT_U32 counter = 0;
            
    limit = _CoreSet_iterate(rlm->sources, &state, items, 8);
    while (limit > 0) 
    {
        do 
        {
            CoreRunLoopSourceRef src = state.items[counter++];
            if (__CoreRunLoopSource_isValid(src) 
                && __CoreRunLoopSource_isSignaled(src))
            {
                if (*sources == null)
                {
                    // this is the first found source
                    *sources = Core_retain(src);
                }
                else if (Core_getClassID(*sources) == CoreRunLoopSourceID)
                {
                    // the second one was found, need to create an array
                    CoreArrayRef array = CoreArray_create(
                        CORE_ALLOCATOR_SYSTEM, 0, &CoreArrayCoreCallbacks
                    );
                    if (array != null)
                    {
                        CoreArray_addValue(array, *sources);
                        CoreArray_addValue(array, src);
                        Core_release(*sources);
                        *sources = array;
                    }
                }
                else
                {
                    // an array was already created, so just add the item to it
                    CoreArray_addValue((CoreArrayRef) *sources, src);
                }
            }                    
        } 
        while (counter < limit);
        limit = _CoreSet_iterate(rlm->sources, &state, items, 8);
    }
}

static void __CoreRunLoop_collectSourcesInMode(
    const void * value,
    void * context
)
{
    CoreRunLoopModeRef rlm = (CoreRunLoopModeRef) value;
    
    __CoreRunLoopMode_lock(rlm);
    if ((rlm->sources != null) && (CoreSet_getCount(rlm->sources) > 0))
    {
        //__CoreRunLoop_collectSources(rlm, context);
        CoreSet_applyFunction(
            rlm->sources, __CoreRunLoop_collectSources, context
        );
    }
    if ((rlm->submodes != null) && (CoreSet_getCount(rlm->submodes) > 0))
    {
        CoreSet_applyFunction(
            rlm->submodes, __CoreRunLoop_collectSourcesInMode, context
        );   
    }
    __CoreRunLoopMode_unlock(rlm);
}

/* mode is locked on entry and exit */
static CoreBOOL __CoreRunLoop_doSources(
    CoreRunLoopRef rl,
    CoreRunLoopModeRef rlm
)
{
    CoreBOOL result = false;
    CoreObjectRef sources = null;
    
    if ((rlm->sources != null) && (CoreSet_getCount(rlm->sources) > 0))
    {
        //__CoreRunLoop_collectSources(rlm, &sources);
        CoreSet_applyFunction(
            rlm->sources, __CoreRunLoop_collectSources, &sources
        );
    }

    // now get sources from all submodes
    if ((rlm->submodes != null) && (CoreSet_getCount(rlm->submodes) > 0))
    {
        CoreSet_applyFunction(
            rlm->submodes, __CoreRunLoop_collectSourcesInMode, &sources
        );
    }

    if (sources != null)
    {
        __CoreRunLoopMode_unlock(rlm);
        if (Core_getClassID(sources) == CoreRunLoopSourceID)
        {
            CoreRunLoopSourceRef src = (CoreRunLoopSourceRef) sources;
            __CoreRunLoopSource_lock(src);
            __CoreRunLoopSource_setSignaled(src, false);
            if (__CoreRunLoopSource_isValid(src))
            {
                __CoreRunLoopSource_unlock(src);
                if (src->delegate.perform != null)
                {
                    src->delegate.perform(src->userInfo.info); // callout
                }
                result = true;
            }
            else
            {
                __CoreRunLoopSource_unlock(src);
            }
        }
        else
        {
            CoreArrayRef array = (CoreArrayRef) sources;
            CoreINT_U32 idx, n;
            
            n = CoreArray_getCount(array);
            CoreArray_sortValues(
                array, CoreRange_make(0, n), __CoreRunLoopSourceComparator  
            );
            
            for (idx = 0; idx < n; idx++)
            {
                CoreRunLoopSourceRef src;
                
                src = CoreArray_getValueAtIndex(array, idx);
                __CoreRunLoopSource_lock(src);
                __CoreRunLoopSource_setSignaled(src, false);
                if (__CoreRunLoopSource_isValid(src))
                {
                    __CoreRunLoopSource_unlock(src);
                    if (src->delegate.perform != null)
                    {
                        src->delegate.perform(src->userInfo.info); // callout
                    }
                    result = true;
                }
                else
                {
                    __CoreRunLoopSource_unlock(src);
                }
            }
        }
        Core_release(sources);
        __CoreRunLoopMode_lock(rlm);         
    }
    
    return result;
}


/* mode is locked on entry and exit */
static CoreBOOL __CoreRunLoop_doSources_old(
    CoreRunLoopRef rl,
    CoreRunLoopModeRef rlm
)
{
    CoreBOOL result = false;
    CoreObjectRef sources = null;
    
    if ((rlm->sources != null) && (CoreSet_getCount(rlm->sources) > 0))
    {
        CoreSet_applyFunction(
            rlm->sources, __CoreRunLoop_collectSources, &sources
        );
    }

    // now get sources from all submodes
    if ((rlm->submodes != null) && (CoreSet_getCount(rlm->submodes) > 0))
    {
        CoreSet_applyFunction(
            rlm->submodes, __CoreRunLoop_collectSourcesInMode, &sources
        );
    }
    
    if (sources != null)
    {
        __CoreRunLoopMode_unlock(rlm);
        if (Core_getClassID(sources) == CoreRunLoopSourceID)
        {
            CoreRunLoopSourceRef src = (CoreRunLoopSourceRef) sources;
            __CoreRunLoopSource_lock(src);
            __CoreRunLoopSource_setSignaled(src, false);
            if (__CoreRunLoopSource_isValid(src))
            {
                __CoreRunLoopSource_unlock(src);
                if (src->delegate.perform != null)
                {
                    src->delegate.perform(src->userInfo.info); // callout
                }
                result = true;
            }
            else
            {
                __CoreRunLoopSource_unlock(src);
            }
        }
        else
        {
            CoreArrayRef array = (CoreArrayRef) sources;
            CoreINT_U32 idx, n;
            
            n = CoreArray_getCount(array);
            CoreArray_sortValues(
                array, CoreRange_make(0, n), __CoreRunLoopSourceComparator    
            );
            
            for (idx = 0; idx < n; idx++)
            {
                CoreRunLoopSourceRef src;
                
                src = CoreArray_getValueAtIndex(array, idx);
                __CoreRunLoopSource_lock(src);
                __CoreRunLoopSource_setSignaled(src, false);
                if (__CoreRunLoopSource_isValid(src))
                {
                    __CoreRunLoopSource_unlock(src);
                    if (src->delegate.perform != null)
                    {
                        src->delegate.perform(src->userInfo.info); // callout
                    }
                    result = true;
                }
                else
                {
                    __CoreRunLoopSource_unlock(src);
                }
            }
        }
        Core_release(sources);
        __CoreRunLoopMode_lock(rlm);         
    }
    
    return result;
}


/* mode is locked */
static CoreRunLoopResult 
__CoreRunLoop_runInMode(
    CoreRunLoopRef rl,
    CoreRunLoopModeRef rlm,
    CoreINT_S64 time,
    CoreBOOL returnAfterHandle
)
{
    CoreINT_U32 result = 0;
    CoreBOOL poll = false;

    // check run loop state...
    if (__CoreRunLoop_isStopped(rl))
    {
        result = CORE_RUN_LOOP_STOPPED;
    }
    
    if (time <= 0)
    {
        poll = true;
    }
    
    while (result == 0)
    {
        CoreBOOL sourceHandled = false;
        CoreRunLoopSleepResult sleepResult;
        
        if ((rlm->observerMask & CORE_RUN_LOOP_CHECK_SOURCES) ||
            (rlm->submodes != null))
        {
            __CoreRunLoop_doObservers(rl, rlm, CORE_RUN_LOOP_CHECK_SOURCES);
        }
        sourceHandled = __CoreRunLoop_doSources(rl, rlm);

        if (sourceHandled && returnAfterHandle)
        {
            poll = true;
        }
        
        if (!poll /*|| (CoreSet_count(mode->timers) > 0)*/)
        {
            // sleep...
            if ((rlm->observerMask & CORE_RUN_LOOP_SLEEP) ||
                (rlm->submodes != null))
            {
                __CoreRunLoop_doObservers(rl, rlm, CORE_RUN_LOOP_SLEEP);
            }
            __CoreRunLoopMode_unlock(rlm);
            sleepResult = __CoreRunLoop_wait(rl, rlm, time);
            __CoreRunLoop_lock(rl);
            __CoreRunLoopMode_lock(rlm);
            __CoreRunLoop_unlock(rl);
            if ((rlm->observerMask & CORE_RUN_LOOP_WAKE_UP) ||
                (rlm->submodes != null))
            {
                __CoreRunLoop_doObservers(rl, rlm, CORE_RUN_LOOP_WAKE_UP);
            }
            if ((rlm->observerMask & CORE_RUN_LOOP_CHECK_TIMERS) ||
                (rlm->submodes != null))
            {
                __CoreRunLoop_doObservers(rl, rlm, CORE_RUN_LOOP_CHECK_TIMERS);
            }
            (void) __CoreRunLoop_doTimers(rl, rlm);
            
        }
        poll = false;
    

        //    
        // Evaluate the result...
        //
        if (sourceHandled && returnAfterHandle)
        {
            result = CORE_RUN_LOOP_SOURCE_HANDLED;
        }
        else if (sleepResult == CORE_RUN_LOOP_SLEEP_TIMED_OUT)
        {
            result = CORE_RUN_LOOP_TIMED_OUT;
        }
        else if (__CoreRunLoop_isStopped(rl))
        {
            result = CORE_RUN_LOOP_STOPPED;
        }
        else if (__CoreRunLoopMode_isEmpty(rlm))
        {
            result = CORE_RUN_LOOP_FINISHED;
        }
        else
        {
            // e.g. CORE_RUN_LOOP_SLEEP_TIMEOUT_FIRED... run another loop
        }
    } // while (result == 0)
    
    return result;
}


static CoreRunLoopResult 
__CoreRunLoop_run(
    CoreRunLoopRef rl,
    CoreImmutableStringRef modeName,
    CoreINT_S64 time,
    CoreBOOL returnAfterHandle
)
{
    CoreINT_U32 result = CORE_RUN_LOOP_FINISHED;
    CoreRunLoopModeRef rlm;

    __CoreRunLoop_lock(rl);
    rlm = __CoreRunLoop_findMode(rl, modeName, false);
    if (rlm != null)
    {
        __CoreRunLoopMode_lock(rlm);
        
        if (!__CoreRunLoopMode_isEmpty(rlm) || (time > 0))
        {
            rl->currentMode = rlm;
            __CoreRunLoop_unlock(rl);

            if ((rlm->observerMask & CORE_RUN_LOOP_ENTRY) ||
                (rlm->submodes != null))
            {
                __CoreRunLoop_doObservers(rl, rlm, CORE_RUN_LOOP_ENTRY);
            }
            result = __CoreRunLoop_runInMode(rl, rlm, time, returnAfterHandle);
            if ((rlm->observerMask & CORE_RUN_LOOP_EXIT) ||
                (rlm->submodes != null))
            {
                __CoreRunLoop_doObservers(rl, rlm, CORE_RUN_LOOP_EXIT);
            }
        }
        else
        {
            __CoreRunLoop_unlock(rl);
        }
        __CoreRunLoopMode_unlock(rlm);
    }
    else
    {
        __CoreRunLoop_unlock(rl);
    }        
    
    return result;
}




/*
 * Storage of  <threadID, run_loop_ref>  pairs. 
 */ 
static CoreSpinLock __CoreRunLoopRegistryLock = CORE_SPIN_LOCK_INIT;
static CoreDictionaryRef __CoreRunLoopRegistry = null;
CoreImmutableStringRef CORE_RUN_LOOP_MODE_DEFAULT = null;
static const char * __CoreRunLoopModeDefaultString = "CoreRunLoopModeDefault";

/*
static CoreRunLoopRef
__CoreRunLoop_getRunLoop(void)
{
    CoreRunLoopRef result;
    CoreINT_U32 threadID = Core_getThreadID();
    
    CoreSpinLock_lock(&__CoreRunLoopRegistryLock);
    result = CoreDictionary_getValue(
        __CoreRunLoopRegistry, 
        threadID
    );
    CoreSpinLock_unlock(&__CoreRunLoopRegistryLock);
    
    return result;
}
*/
static CoreRunLoopRef
__CoreRunLoop_getRunLoop(void)
{
    CoreRunLoopRef result = NULL;
    
#if defined(__LINUX__)
    result = pthread_getspecific(__CoreRunLoopThreadKey);
#elif defined(__WIN32__)
    result = TlsGetValue(__CoreRunLoopThreadKey);
#endif
    if (CORE_UNLIKELY(result == NULL)) 
    {
        result = CoreRunLoop_create();
    }

    return result;
}


static CoreRunLoopRef
__CoreRunLoop_init(void)
{
    CoreRunLoopRef result = null;
    CoreINT_U32 size = sizeof(struct __CoreRunLoop);
    
    CORE_DUMP_TRACE(__FUNCTION__);
    
    result = CoreRuntime_createObject(
        CORE_ALLOCATOR_SYSTEM, CoreRunLoopID, size
    );
    if (result != null)
    {
        (void) CoreSpinLock_init(&result->lock);
        result->modes = null;
        result->currentMode = null;

        #if defined(__LINUX__)
        {
            int ok;
            
            ok = socketpair(PF_LOCAL, SOCK_DGRAM, 0, result->selfpipe);
            if (ok == 0)
            {
                unsigned int unblocking = 1;
                int oldFlag = fcntl(result->selfpipe[0], F_GETFL, 0);
                
                fcntl(result->selfpipe[0], F_SETFL, oldFlag | O_NONBLOCK);
                fcntl(result->selfpipe[1], F_SETFL, oldFlag | O_NONBLOCK);
            }
        }
        #elif defined(__WIN32__)
        result->port = CreateEvent(NULL, true, false, NULL);
        #endif
        
        result->modes = CoreSet_create(
            CORE_ALLOCATOR_SYSTEM, 0, &CoreSetValueCoreCallbacks
        );
        if (result->modes != null)
        {
            (void) __CoreRunLoop_findMode(
                result, CORE_RUN_LOOP_MODE_DEFAULT, true
            );
        }
    }       
    
    return result;
}


static void
__CoreRunLoop_cleanupSources(const void * value, void * context)
{
    CoreRunLoopModeRef mode = (CoreRunLoopModeRef) value;
    CoreRunLoopRef runloop = (CoreRunLoopRef) context;
    CoreINT_U32 count;
    
    __CoreRunLoop_lock(runloop);
    __CoreRunLoopMode_lock(mode);
    __CoreRunLoop_unlock(runloop);

    count = CoreSet_getCount(mode->sources);
    if ((mode->sources != null) && (count > 0))
    {
        CoreObjectRef buffer[32];
        CoreObjectRef * sources = buffer;
        CoreINT_U32 idx;
        
        if (count > 32)
        {
            sources = CoreAllocator_allocate(
                CORE_ALLOCATOR_SYSTEM, count * sizeof(CoreObjectRef)
            );
        }
        CoreSet_copyValues(mode->sources, sources);
        
        // Retain first
        for (idx = 0; idx < count; idx++)
        {
            Core_retain(sources[idx]);
        }
        
        // Now clear the sources in mode
        CoreSet_clear(mode->sources);
        
        // Now cancel all sources and release them
        for (idx = 0; idx < count; idx++)
        {
            __CoreRunLoopSource_cancel(sources[idx], runloop, mode);
            Core_release(sources[idx]);
        }
        
        if (sources != buffer)
        {
            CoreAllocator_deallocate(CORE_ALLOCATOR_SYSTEM, sources);
        }
    }
    
    __CoreRunLoopMode_unlock(mode);
}

static void
__CoreRunLoop_cleanupTimers(const void * value, void * context)
{
    CoreRunLoopModeRef mode = (CoreRunLoopModeRef) value;
    CoreRunLoopRef runloop = (CoreRunLoopRef) context;
    CoreINT_U32 count;
    
    __CoreRunLoop_lock(runloop);
    __CoreRunLoopMode_lock(mode);
    __CoreRunLoop_unlock(runloop);

    count = CoreSet_getCount(mode->timers);
    if ((mode->timers != null) && (count > 0))
    {
        CoreObjectRef buffer[32];
        CoreObjectRef * timers = buffer;
        CoreINT_U32 idx;
        
        if (count > 32)
        {
            timers = CoreAllocator_allocate(
                CORE_ALLOCATOR_SYSTEM, count * sizeof(CoreObjectRef)
            );
        }
        CoreSet_copyValues(mode->timers, timers);
        
        // Retain first
        for (idx = 0; idx < count; idx++)
        {
            Core_retain(timers[idx]);
        }
        
        // Now clear the timers in mode
        CoreSet_clear(mode->timers);
        
        // Now cancel all timers and release them
        for (idx = 0; idx < count; idx++)
        {
            __CoreTimer_cancel(timers[idx], runloop, mode);
            Core_release(timers[idx]);
        }
        
        if (timers != buffer)
        {
            CoreAllocator_deallocate(CORE_ALLOCATOR_SYSTEM, timers);
        }
    }
    
    __CoreRunLoopMode_unlock(mode);
}

static void
__CoreRunLoop_cleanupObservers(const void * value, void * context)
{
    CoreRunLoopModeRef mode = (CoreRunLoopModeRef) value;
    CoreRunLoopRef runloop = (CoreRunLoopRef) context;
    CoreINT_U32 count;
    
    __CoreRunLoop_lock(runloop);
    __CoreRunLoopMode_lock(mode);
    __CoreRunLoop_unlock(runloop);

    count = CoreSet_getCount(mode->observers);
    if ((mode->observers != null) && (count > 0))
    {
        CoreObjectRef buffer[32];
        CoreObjectRef * observers = buffer;
        CoreINT_U32 idx;
        
        if (count > 32)
        {
            observers = CoreAllocator_allocate(
                CORE_ALLOCATOR_SYSTEM, count * sizeof(CoreObjectRef)
            );
        }
        CoreSet_copyValues(mode->observers, observers);
        
        // Retain first
        for (idx = 0; idx < count; idx++)
        {
            Core_retain(observers[idx]);
        }
        
        // Now clear the observers in mode
        CoreSet_clear(mode->observers);
        
        // Now cancel all observers and release them
        for (idx = 0; idx < count; idx++)
        {
            __CoreRunLoopObserver_cancel(observers[idx]);
            Core_release(observers[idx]);
        }
        
        if (observers != buffer)
        {
            CoreAllocator_deallocate(CORE_ALLOCATOR_SYSTEM, observers);
        }
    }
    
    __CoreRunLoopMode_unlock(mode);
}

static void
__CoreRunLoop_cleanup(CoreRunLoopRef rl)
{
    __CoreRunLoop_setDeallocating(rl, true);
    
    if (rl->modes != null)
    {
        CoreSet_applyFunction(rl->modes, __CoreRunLoop_cleanupSources, rl);
        CoreSet_applyFunction(rl->modes, __CoreRunLoop_cleanupTimers, rl);
        CoreSet_applyFunction(rl->modes, __CoreRunLoop_cleanupObservers, rl);
    }
    
    __CoreRunLoop_lock(rl);
    if (rl->modes != null)
    {
        Core_release(rl->modes);
    }
    rl->currentMode = null;
#if defined(__WIN32__)    
    CloseHandle(rl->port);
#elif defined(__LINUX__)
    close(rl->selfpipe[0]);
    close(rl->selfpipe[1]);
#endif    
    __CoreRunLoop_unlock(rl);
}



static const CoreClass __CoreRunLoopClass =
{
    0x00,                            // version
    "CoreRunLoop",                   // name
    NULL,                            // init
    NULL,                            // copy
    __CoreRunLoop_cleanup,           // cleanup
    NULL,                            // equal
    NULL,                            // hash
    NULL,//__CoreRunLoop_getCopyOfDescription // getCopyOfDescription
};


/* CORE_PROTECTED */ void
CoreRunLoop_initialize(void)
{
    CoreTimerID = CoreRuntime_registerClass(&__CoreTimerClass);
    CoreRunLoopSourceID = CoreRuntime_registerClass(&__CoreRunLoopSourceClass);
    CoreRunLoopObserverID = CoreRuntime_registerClass(&__CoreRunLoopObserverClass);
    CoreRunLoopModeID = CoreRuntime_registerClass(&__CoreRunLoopModeClass);
    CoreRunLoopID = CoreRuntime_registerClass(&__CoreRunLoopClass);
    
    if (__CoreRunLoopThreadKey == ~0) 
    {
#if defined(__LINUX__)
        while (pthread_key_create(__CoreRunLoopThreadKey, NULL) != 0)
        {
            sched_yield();
        }
#elif defined(__WIN32__)
        __CoreRunLoopThreadKey = TlsAlloc();
#endif
    }
}




/* CORE_PUBLIC */ CoreRunLoopRef 
CoreRunLoop_create(void)
{
    CoreRunLoopRef result = null;
    CoreINT_U32 threadID = Core_getThreadID();
    
    CORE_DUMP_TRACE(__FUNCTION__);
    
    CoreSpinLock_lock(&__CoreRunLoopRegistryLock);
    if (__CoreRunLoopRegistry == null)
    {
        __CoreRunLoopRegistry = CoreDictionary_create(
            CORE_ALLOCATOR_SYSTEM, 0, null, null
        );
    }
    if (CORE_RUN_LOOP_MODE_DEFAULT == null)
    {
        CORE_RUN_LOOP_MODE_DEFAULT = CoreString_createImmutableWithASCII(
            CORE_ALLOCATOR_SYSTEM, 
            __CoreRunLoopModeDefaultString,
            strlen(__CoreRunLoopModeDefaultString)
        );
    }
    if (__CoreRunLoopRegistry != null)
    {
        result = CoreDictionary_getValue(__CoreRunLoopRegistry, threadID);
        CoreSpinLock_unlock(&__CoreRunLoopRegistryLock);
        if (result == null)
        {
            result = __CoreRunLoop_init();
            CoreSpinLock_lock(&__CoreRunLoopRegistryLock);
            CoreDictionary_addValue(
                __CoreRunLoopRegistry,
                threadID,
                result
            );
            CoreSpinLock_unlock(&__CoreRunLoopRegistryLock);
            
#if defined(__LINUX__)
            pthread_setspecific(__CoreRunLoopThreadKey, result);
#elif defined(__WIN32)
            TlsSetValue(__CoreRunLoopThreadKey, result);
#endif                
        }
    }
    else
    {
        CoreSpinLock_unlock(&__CoreRunLoopRegistryLock);
    }
    
    return result;
}


/* CORE_PUBLIC */ void 
CoreRunLoop_run(void)
{
    CoreRunLoopRef runLoop;
    
    CORE_DUMP_TRACE(__FUNCTION__);

    runLoop = __CoreRunLoop_getRunLoop();    
    if (runLoop != null)
    {
        CoreINT_U32 result;
        
        do
        {
            result = __CoreRunLoop_run(
                runLoop,
                CORE_RUN_LOOP_MODE_DEFAULT,
                CoreINT_S64_MAX,
                false
            );
        }
        while ((result != CORE_RUN_LOOP_STOPPED) 
               || (result != CORE_RUN_LOOP_FINISHED));
    }
}


/* CORE_PUBLIC */ CoreRunLoopResult
CoreRunLoop_runInMode(
    CoreImmutableStringRef mode, 
    CoreINT_S64 delay,
    CoreBOOL returnAfterHandle)
{
    CoreRunLoopResult result = 0;
    CoreRunLoopRef runLoop;
    
    CORE_DUMP_TRACE(__FUNCTION__);

    runLoop = __CoreRunLoop_getRunLoop();    
    if (runLoop != null)
    {
        result = __CoreRunLoop_run(
            runLoop, mode, delay, returnAfterHandle
        );
    }
    
    return result;
}


/* CORE_PUBLIC */ void 
CoreRunLoop_stop(CoreRunLoopRef rl)
{
    CORE_DUMP_OBJ_TRACE(rl, __FUNCTION__);
    
    // RunLoop will be stopped right after it enters its loop, i.e. 
    // it is not synchronous.
    __CoreRunLoop_lock(rl);
    __CoreRunLoop_setStopped(rl, true);
    __CoreRunLoop_unlock(rl);
    __CoreRunLoop_wakeUp(rl);
}


/* CORE_PUBLIC */ void 
CoreRunLoop_wakeUp(CoreRunLoopRef rl)
{
    CORE_DUMP_OBJ_TRACE(rl, __FUNCTION__);

    __CoreRunLoop_wakeUp(rl);
}


/* CORE_PUBLIC */ CoreRunLoopRef 
CoreRunLoop_getCurrent(void)
{
    CORE_DUMP_TRACE(__FUNCTION__);
    return __CoreRunLoop_getRunLoop();
}


static CoreImmutableStringRef
__CoreRunLoop_getCurrentModeName_nolock(CoreRunLoopRef rl)
{
    return rl->currentMode->name;
}

/* CORE_PUBLIC */ CoreImmutableStringRef 
CoreRunLoop_getCurrentModeName(CoreRunLoopRef rl)
{
    CoreImmutableStringRef result = null;
    
    CORE_DUMP_OBJ_TRACE(rl, __FUNCTION__);

    __CoreRunLoop_lock(rl);
    result = rl->currentMode->name;
    __CoreRunLoop_unlock(rl);
    
    return result;
}


/* CORE_PUBLIC */ void
CoreRunLoop_addMode(
    CoreRunLoopRef rl,
    CoreImmutableStringRef modeName,
    CoreImmutableStringRef toModeName
)
{
    CoreRunLoopModeRef rlm = null;
    
    CORE_DUMP_OBJ_TRACE(rl, __FUNCTION__);

    /* check run loop state...*/
    if (__CoreRunLoop_isDeallocating(rl)) return;
    
    if (__CoreRunLoop_findMode(rl, modeName, false) != null)
    {
        // already included!
    }
    else if (toModeName == null)
    {
        __CoreRunLoop_lock(rl);
        rlm = __CoreRunLoop_findMode(rl, modeName, true);
        __CoreRunLoop_unlock(rl);
    }
    else
    {
        if (!Core_equal(modeName, toModeName))
        {
            CoreBOOL lockAcquired;
            
            __CoreRunLoop_lock(rl);
            lockAcquired = true;
            rlm = __CoreRunLoop_findMode(rl, toModeName, false);
            if (rlm != null)
            {
                CoreRunLoopModeRef subrlm;
                
                subrlm = __CoreRunLoop_findMode(rl, modeName, true);
                if (subrlm != null)
                {
                    __CoreRunLoopMode_lock(rlm);
                    __CoreRunLoop_unlock(rl);
                    lockAcquired = false;
                    if (rlm->submodes == null)
                    {
                        CoreSet_create(
                            Core_getAllocator(rlm), 0, &CoreSetValueCoreCallbacks
                        );
                    }
                    if (rlm->submodes != null)
                    {
                        CoreSet_addValue(rlm->submodes, subrlm);
                    }
                }
            }
            
            // Well, just to avoid several else-clauses for ifs above, 
            // I used a variable indicating the state of RL's lock.
            if (lockAcquired)
            {
                __CoreRunLoop_unlock(rl);
            }
        }
    }
}


/* CORE_PUBLIC */ void 
CoreRunLoop_removeSubmodeFromMode(
    CoreRunLoopRef rl,
    CoreImmutableStringRef modeName,
    CoreImmutableStringRef fromModeName
)
{
    CORE_DUMP_OBJ_TRACE(rl, __FUNCTION__);

    /* check run loop state...*/
    
    if (!Core_equal(modeName, fromModeName))
    {
        CoreRunLoopModeRef rlm = null;
        CoreBOOL canRemove;
        CoreBOOL lockAcquired = true;
        
        __CoreRunLoop_lock(rl);
        
        // Mode can be removed only if this is called on RL's thread OR 
        // when the mode is not a RL's current mode.
        canRemove = (rl == CoreRunLoop_getCurrent()) ||
            (!Core_equal(modeName, __CoreRunLoop_getCurrentModeName_nolock(rl)));
        if (canRemove)
        {
            rlm = __CoreRunLoop_findMode(rl, fromModeName, false);
            if (rlm != null)
            {
                CoreRunLoopModeRef subrlm;
                
                subrlm = __CoreRunLoop_findMode(rl, modeName, true);
                if (subrlm != null)
                {
                    __CoreRunLoopMode_lock(rlm);
                    __CoreRunLoop_unlock(rl);
                    lockAcquired = false;
                    if (rlm->submodes != null)
                    {
                        CoreSet_removeValue(rlm->submodes, subrlm);
                    }
                    __CoreRunLoopMode_unlock(rlm);
                }
            }
        }
        
        // Well, just to avoid several else-clauses for ifs above, 
        // I used a variable indicating the state of RL's lock.
        if (lockAcquired)
        {
            __CoreRunLoop_unlock(rl);
        }
    }
}


//
// We must ensure that in case of sleeping run loop in the desired mode and
// the sleep was scheduled for longer time than this timer's fireTime, the
// run loop will be woken up.
// NOTE: We check this timer's fireTime against fireTimes of other timers only.
// The timeout argument in any of '_run...' methods is not taken into account.
//
// NOTE2: Or we can wake-up the loop everytime it is sleeping, regardless of
// comparing this timer to the other -- already scheduled -- ones.
// 
/* CORE_PUBLIC */ void 
CoreRunLoop_addTimer(
    CoreRunLoopRef rl,
    CoreTimerRef rlt,
    CoreImmutableStringRef modeName
)
{
    CORE_DUMP_OBJ_TRACE(rl, __FUNCTION__);

    /* check run loop state...*/
    if (__CoreRunLoop_isDeallocating(rl)) return;
            
    if (__CoreTimer_isValid(rlt) 
        && ((rlt->runLoop == null) || (rlt->runLoop == rl)))
    {
        CoreRunLoopModeRef mode;
        CoreBOOL shouldWakeUp = false;

        __CoreRunLoop_lock(rl);

        // 1. If this loop is in sleep for the specified mode...
        if (__CoreRunLoop_isSleeping(rl) 
            && Core_equal(modeName, rl->currentMode->name))
        {
            shouldWakeUp = true;
        }
        mode = __CoreRunLoop_findMode(rl, modeName, true);
        if (mode != null)
        {
            __CoreRunLoopMode_lock(mode);
            __CoreRunLoop_unlock(rl);
            if (CORE_UNLIKELY(mode->timers == null))
            {
                mode->timers = CoreSet_create(
                    Core_getAllocator(mode), 0, &CoreSetValueCoreCallbacks
                );
            }
            if (CORE_LIKELY(mode->timers != null))
            {
                CoreSet_addValue(mode->timers, rlt);
                
                // ... and 2. this timer is the minimal one, ...
                if (shouldWakeUp)
                {
                    CoreINT_S64 minFireTime;
                    
                    minFireTime = __CoreRunLoop_getMinFireTime(rl, mode);
                    shouldWakeUp = (minFireTime == rlt->fireTime);
                }                
                __CoreRunLoopMode_unlock(mode);
                __CoreTimer_schedule(rlt, rl, mode);
            }
            else
            {
                __CoreRunLoopMode_unlock(mode);
            }
        }
        else
        {
            __CoreRunLoop_unlock(rl);
        }
        
        // 3. ... wake up this loop. (is rl still sleeping?)
        if (shouldWakeUp && __CoreRunLoop_isSleeping(rl))
        {
            CoreRunLoop_wakeUp(rl);
        }
    }
}


/* CORE_PUBLIC */ void
CoreRunLoop_removeTimer(    
    CoreRunLoopRef rl,
    CoreTimerRef rlt,
    CoreImmutableStringRef modeName
)
{
    CoreRunLoopModeRef mode;

    CORE_DUMP_OBJ_TRACE(rl, __FUNCTION__);

    /* check run loop state...*/
        
    __CoreRunLoop_lock(rl);
    mode = __CoreRunLoop_findMode(rl, modeName, false);
    if (mode != null)
    {
        __CoreRunLoopMode_lock(mode);
        __CoreRunLoop_unlock(rl);
        if ((mode->timers != null) && (CoreSet_getCount(mode->timers) > 0))
        {
            Core_retain(rlt); // for sure it doesn't free when removing...
            CoreSet_removeValue(mode->timers, rlt);
            __CoreRunLoopMode_unlock(mode);
            __CoreTimer_cancel(rlt, rl, mode);
            Core_release(rlt);
        }
        else
        {
            __CoreRunLoopMode_unlock(mode);
        }
    }    
}

/* CORE_PUBLIC */ void 
CoreRunLoop_addSource(
    CoreRunLoopRef rl,
    CoreRunLoopSourceRef rls,
    CoreImmutableStringRef modeName
)
{
    CoreRunLoopModeRef mode;

    CORE_DUMP_OBJ_TRACE(rl, __FUNCTION__);
    
    /* check run loop state...*/
    if (__CoreRunLoop_isDeallocating(rl)) return;
            
    __CoreRunLoop_lock(rl);
    mode = __CoreRunLoop_findMode(rl, modeName, true);
    if (mode != null)
    {
        __CoreRunLoopMode_lock(mode);
        __CoreRunLoop_unlock(rl);
        if (CORE_UNLIKELY(mode->sources == null))
        {
            mode->sources = CoreSet_create(
                Core_getAllocator(mode), 
                0, 
                &CoreSetValueCoreCallbacks
            );
        }
        if (CORE_LIKELY(mode->sources != null))
        {
            CoreSet_addValue(mode->sources, rls);
            __CoreRunLoopMode_unlock(mode); // unlock before callout
            __CoreRunLoopSource_schedule(rls, rl, mode); // callout
        }
        else
        {
            __CoreRunLoopMode_unlock(mode);
        }
    }
    else
    {
        __CoreRunLoop_unlock(rl);
    }
}


/* CORE_PUBLIC */ void
CoreRunLoop_removeSource(    
    CoreRunLoopRef rl,
    CoreRunLoopSourceRef rls,
    CoreImmutableStringRef modeName
)
{
    CoreRunLoopModeRef mode;

    CORE_DUMP_OBJ_TRACE(rl, __FUNCTION__);
    
    /* check run loop state...*/
        
    __CoreRunLoop_lock(rl);
    mode = __CoreRunLoop_findMode(rl, modeName, false);
    if (mode != null)
    {
        __CoreRunLoopMode_lock(mode);
        __CoreRunLoop_unlock(rl);
        if ((mode->sources != null) 
            && (CoreSet_containsValue(mode->sources, rls) > 0))
        {
            Core_retain(rls); // for sure it doesn't free when removing...
            CoreSet_removeValue(mode->sources, rls);
            __CoreRunLoopMode_unlock(mode);
            __CoreRunLoopSource_cancel(rls, rl, mode);
            Core_release(rls);
        }
        else
        {
            __CoreRunLoopMode_unlock(mode);
        }
    }
    else
    {
        __CoreRunLoop_unlock(rl);
    }    
}


/* CORE_PUBLIC */ CoreBOOL
CoreRunLoop_containsSource(    
    CoreRunLoopRef rl,
    CoreRunLoopSourceRef rls,
    CoreImmutableStringRef mode
)
{
    CoreBOOL result = false;
    CoreRunLoopModeRef rlm = NULL;
    
    __CoreRunLoop_lock(rl);
    rlm = __CoreRunLoop_findMode(rl, mode, false);
    if (rlm != NULL)
    {
        __CoreRunLoop_unlock(rl);
        if (rlm->sources != NULL)
        {
            result = CoreSet_containsValue(rlm->sources, rls);
        }
        __CoreRunLoopMode_lock(rlm);
    }
    else
    {
        __CoreRunLoop_unlock(rl);
    }
    
    return result;
}


/* CORE_PUBLIC */ void 
CoreRunLoop_addObserver(
    CoreRunLoopRef rl,
    CoreRunLoopObserverRef rlo,
    CoreImmutableStringRef modeName
)
{
    CoreRunLoopModeRef mode;

    CORE_DUMP_OBJ_TRACE(rl, __FUNCTION__);
    
    /* check run loop state...*/
    if (__CoreRunLoop_isDeallocating(rl)) return;
            
    __CoreRunLoop_lock(rl);
    mode = __CoreRunLoop_findMode(rl, modeName, true);
    if (mode != null)
    {
        __CoreRunLoopMode_lock(mode);
        __CoreRunLoop_unlock(rl);
        if (mode->observers == null)
        {
            mode->observers = CoreSet_create(
                Core_getAllocator(mode), 0, &CoreSetValueCoreCallbacks
            );
        }
        if (mode->observers != null)
        {
            CoreSet_addValue(mode->observers, rlo);
            mode->observerMask |= rlo->activities;
            __CoreRunLoopMode_unlock(mode); 
            __CoreRunLoopObserver_schedule(rlo, rl, mode); 
        }
        else
        {
            __CoreRunLoopMode_unlock(mode);
        }
    }
    else
    {
        __CoreRunLoop_unlock(rl);
    }
}


/* CORE_PUBLIC */ void
CoreRunLoop_removeObserver(    
    CoreRunLoopRef rl,
    CoreRunLoopObserverRef rlo,
    CoreImmutableStringRef modeName
)
{
    CoreRunLoopModeRef mode;

    CORE_DUMP_OBJ_TRACE(rl, __FUNCTION__);

    /* check run loop state...*/
        
    __CoreRunLoop_lock(rl);
    mode = __CoreRunLoop_findMode(rl, modeName, false);
    if (mode != null)
    {
        __CoreRunLoopMode_lock(mode);
        __CoreRunLoop_unlock(rl);
        if ((mode->observers != null) && (CoreSet_getCount(mode->observers) > 0))
        {
            Core_retain(rlo); // for sure it doesn't free when removing...
           CoreSet_removeValue(mode->observers, rlo);
            __CoreRunLoopMode_unlock(mode);
            __CoreRunLoopObserver_cancel(rlo);
            Core_release(rlo);
        }
        else
        {
            __CoreRunLoopMode_unlock(mode);
        }
    }    
}

static void
__CoreRunLoop_collectModeNames(const void * value, void * context)
{
    CoreRunLoopModeRef rlm = (CoreRunLoopModeRef) value;
    CoreArrayRef array = (CoreArrayRef) context;
    CoreArray_addValue(array, rlm->name);
}


/* CORE_PUBLIC */ CoreImmutableArrayRef
CoreRunLoop_getCopyOfModes(CoreRunLoopRef rl)
{
    CoreArrayRef result = null;

    CORE_DUMP_OBJ_TRACE(rl, __FUNCTION__);
        
    __CoreRunLoop_lock(rl);
    result = CoreArray_create(
        CORE_ALLOCATOR_SYSTEM, 
        CoreSet_getCount(rl->modes),
        &CoreArrayCoreCallbacks
    );
    CoreSet_applyFunction(rl->modes, __CoreRunLoop_collectModeNames, result);
    __CoreRunLoop_unlock(rl);
    
    return result;   
}


#undef CORE_NSEC_PER_SEC

