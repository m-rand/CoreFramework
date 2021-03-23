
/*
    RunLoop: A general purpose Reactor-pattern-based class.
    What it does, is basically a demultiplexing of inputs and timers and
    dispatching events to clients. However, compared to a simple Reactor 
    pattern, it provides several advanced features, like:
    - You can specify mode(s) in which a run loop runs -- and therefore 
        discriminate sources and timers that are not included in the mode.
    - Run loop generates notifications about its current state, which makes
        possible for run-loop's clients to do some necessary stuff, e.g.
        before going to sleep or after waking up.     
    
    RunLoop is basically a wrapper around some system-dependent demultiplexor, 
    like select/poll or WaitForMultipleObjects, but with following differences:
    (select + WaitForMultipleObjects)
    - You can register both system sources -- like descriptors, sockets -- 
        and your custom sources (responsible for handling whatever events 
        you like) together.
    - Your event handlers are already included in registered sources. When
        an event happen, your handler is called right from within the RunLoop.
    (select) 
    - You can register so many timers you can (your system allows to).
    
    Notes:
    - one source can be installed on one run-loop only
    - loop can run in several modes at a time
    - all callbacks should be non-blocking (to keep run-loop responsive)
    
    Performance notes:
    While it could be seen as a huge performance hog, thanks to its complexness
    and broad customization possibilities, using RunLoop could be a huge win
    for all event-driven application. You can register event handlers for 
    practically any type of events which gives you a possibility to keep number
    of threads in your application at minimum. Using run loop observers gives
    you a chance to manage some other application stuff according to actual 
    load of ready-to-dispatch events (e.g. postpone some low priority tasks 
    to a time when a thread goes to idle).
    The implementation is quite complex mainly because of using modes.
    Without modes all the code could be _very_ simplified with a big impact 
    on better performance (e.g. we would use only a queue of signalled events
    instead of collecting and checking sources each loop). However, using modes
    such greatly facilitates to filter some events -- which would have to be
    implemented externally by every RunLoop's user in all a bit complex 
    situations anyway -- that embodying modes into runloop internals was granted
    an advantage over performance in ordinary cases.   
    
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
#include "CoreAlgorithms.h"

#if defined(__LINUX__)
#include <fcntl.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h> 
#include <sys/epoll.h> 
#include <poll.h>
#include <unistd.h>
#elif defined(__WIN32__)
#undef __STDC__
#include <windows.h>
#include <winbase.h>
#include <process.h>
#define __STDC__
#endif


static CoreClassID CoreRunLoopSourceID = CORE_CLASS_ID_UNKNOWN;
static CoreClassID CoreRunLoopObserverID = CORE_CLASS_ID_UNKNOWN;
static CoreClassID CoreRunLoopModeID = CORE_CLASS_ID_UNKNOWN;
static CoreClassID CoreRunLoopID = CORE_CLASS_ID_UNKNOWN;



#if defined(__WIN32__)
static DWORD __CoreRunLoopThreadKey = ~0;
#elif defined(__LINUX__)
static pthread_key_t __CoreRunLoopThreadKey = ~0;
#endif
 

#if defined(__LINUX__)
    #define CORE_SYSTEM_DESCRIPTOR_INVALID    (int)-1
    #define CORE_MAX_STACK_DESCRIPTORS  64         
#elif defined(__WIN32__)
    #define CORE_SYSTEM_DESCRIPTOR_INVALID    (PVOID) NULL
    #define CORE_MAX_STACK_DESCRIPTORS  MAXIMUM_WAIT_OBJECTS    
#endif

#if defined(__NIOS__) || defined(__NIOS2__)
    #ifdef CORE_MAX_STACK_DESCRIPTORS 
    #undef CORE_MAX_STACK_DESCRIPTORS
    #define CORE_MAX_STACK_DESCRIPTORS  32
    #endif
#endif

#if defined(__LINUX__)
#define __CoreRunLoop_waitForEvent_sys __CoreRunLoop_waitForEvent_poll
#elif defined(__WIN32__)
#define __CoreRunLoop_waitForEvent_sys __CoreRunLoop_waitForEvent_win32
#endif


#define CORE_RUN_LOOP_WAIT_TIMEOUT      -1
#define CORE_RUN_LOOP_WAIT_TIMER_FIRED  -2
#define CORE_RUN_LOOP_WAIT_ERROR        ~0

    
#define CORE_RUN_LOOP_RUN_MAX_MODES     3


#if defined(__LINUX__)
CORE_INLINE CoreBOOL
__CoreSystemDescriptor_isValid(CoreSystemDescriptor d)
{
    return (d >= 0);
}
#elif defined(__WIN32__)
CORE_INLINE CoreBOOL
__CoreSystemDescriptor_isValid(CoreSystemDescriptor d)
{
    return (d != INVALID_HANDLE_VALUE);
}
#endif





/*
 *
 *      CORE RUN LOOP SOURCE
 *
 */
 
typedef struct CoreRunLoopCustomSource
{
    CoreINT_S32 _priority;
    CoreINT_S64 _timeout;
} CoreRunLoopCustomSource;

typedef struct CoreRunLoopDescriptorSource
{
    CoreINT_S32 _priority;
    CoreINT_S64 _timeout;
    CoreSystemDescriptor _descriptor;
} CoreRunLoopDescriptorSource;

typedef struct CoreRunLoopTimerSource
{
    CoreINT_S64 _fireTime;
    CoreINT_U32 _period;
    CoreINT_U32 _leeway;    
} CoreRunLoopTimerSource;

struct __CoreRunLoopSource
{
    CoreRuntimeObject _core;
    CoreSpinLock _lock;
    CoreRunLoopRef _runLoop;
    CoreRunLoopSourceDelegate _delegate;
    union
    {
        CoreRunLoopDescriptorSource _descriptor;
        CoreRunLoopCustomSource _custom;
        CoreRunLoopTimerSource _timer;
    } _type;
};

#define CORE_RUN_LOOP_SOURCE_INIT_BIT   0
#define CORE_RUN_LOOP_SOURCE_VALID_BIT  1
#define CORE_RUN_LOOP_SOURCE_SIGNAL_BIT 2
#define CORE_RUN_LOOP_SOURCE_TYPE_START     3
#define CORE_RUN_LOOP_SOURCE_TYPE_LENGTH    2 // cannot be more than 2 !!


CORE_INLINE CoreINT_U32
__CoreRunLoopSource_getInfoBit(CoreRunLoopSourceRef rls, CoreINT_U32 bit)
{
    return CoreBitfield_getValue(
        ((const CoreRuntimeObject *) rls)->info, bit, 1
    );
}
CORE_INLINE void
__CoreRunLoopSource_setInfoBit(
    CoreRunLoopSourceRef rls, CoreINT_U32 bit, CoreBOOL value
)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) rls)->info, bit, 1, value
    );
}

CORE_INLINE CoreBOOL
__CoreRunLoopSource_isInitialized(CoreRunLoopSourceRef src)
{
    return __CoreRunLoopSource_getInfoBit(
        src, CORE_RUN_LOOP_SOURCE_INIT_BIT) == 1;
}
CORE_INLINE CoreBOOL
__CoreRunLoopSource_isValid(CoreRunLoopSourceRef src)
{
    return __CoreRunLoopSource_getInfoBit(
        src, CORE_RUN_LOOP_SOURCE_VALID_BIT) == 1;
}
CORE_INLINE CoreBOOL
__CoreRunLoopSource_isSignaled(CoreRunLoopSourceRef src)
{
    return __CoreRunLoopSource_getInfoBit(
        src, CORE_RUN_LOOP_SOURCE_SIGNAL_BIT) == 1;
}

CORE_INLINE void
__CoreRunLoopSource_setInitialized(CoreRunLoopSourceRef src, CoreBOOL v)
{
    __CoreRunLoopSource_setInfoBit(src, CORE_RUN_LOOP_SOURCE_INIT_BIT, v);
}
CORE_INLINE void
__CoreRunLoopSource_setValid(CoreRunLoopSourceRef src, CoreBOOL v)
{
    __CoreRunLoopSource_setInfoBit(src, CORE_RUN_LOOP_SOURCE_VALID_BIT, v);
}
CORE_INLINE void
__CoreRunLoopSource_setSignaled(CoreRunLoopSourceRef src, CoreBOOL v)
{
    __CoreRunLoopSource_setInfoBit(src, CORE_RUN_LOOP_SOURCE_SIGNAL_BIT, v);
}
CORE_INLINE CoreRunLoopSourceType
__CoreRunLoopSource_getType(CoreRunLoopSourceRef src)
{
    return CoreBitfield_getValue(
        ((const CoreRuntimeObject *) src)->info,
        CORE_RUN_LOOP_SOURCE_TYPE_START,
        CORE_RUN_LOOP_SOURCE_TYPE_LENGTH
    );
}
CORE_INLINE void
__CoreRunLoopSource_setType(CoreRunLoopSourceRef src, CoreRunLoopSourceType t)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) src)->info, 
        CORE_RUN_LOOP_SOURCE_TYPE_START, 
        CORE_RUN_LOOP_SOURCE_TYPE_LENGTH, 
        t
    );
}

CORE_INLINE void 
__CoreRunLoopSource_lock(CoreRunLoopSourceRef src)
{
    CoreSpinLock_lock(&src->_lock);    
}
CORE_INLINE void 
__CoreRunLoopSource_unlock(CoreRunLoopSourceRef src)
{
    CoreSpinLock_unlock(&src->_lock);    
}


static void 
__CoreRunLoopSource_scheduleDescriptor(
    CoreRunLoopSourceRef src,
    CoreRunLoopRef runLoop,
    CoreRunLoopModeRef mode
)
{
#if defined(__LINUX__)
    if (mode->_epollfd == -1)
    {
        mode->_epollfd = epoll_create(8); // the size is ignored -- see man
    }
    if (mode->_epollfd != -1)
    {
        struct epoll_event ev;
        CoreSystemDescriptor d;
        int res;
        
        d = src->_type._descriptor._descriptor;
        ev.events = EPOLLIN;
        ev.data.ptr = src;
        res = epoll_ctl(epollfd, EPOLL_CTL_ADD, d, &ev);
        if (res == -1)
        {
            // TODO error
        }
        else
        {
            mode->_descrCount++;
        }
    }
#elif defined(__WIN32__)
    if (mode->_descrMap == NULL)
    {
        mode->_descrMap = CoreDictionary_create(
            Core_getAllocator(mode), 0, NULL, NULL
        );
    }
    if (mode->_descrMap != NULL)
    {
        CoreSystemDescriptor d = src->_type._descriptor._descriptor;;
        CoreDictionary_addValue(mode->_descrMap, d, src);
    }
#endif
}

static void 
__CoreRunLoopSource_schedule(
    CoreRunLoopSourceRef src,
    CoreRunLoopRef runLoop,
    CoreRunLoopModeRef mode
)
{
    __CoreRunLoopSource_lock(src);
    src->_runLoop = runLoop;
    __CoreRunLoopSource_unlock(src);
    
    switch (__CoreRunLoopSource_getType(src))
    {
        case CORE_RUN_LOOP_SOURCE_CUSTOM:
            break;
        case CORE_RUN_LOOP_SOURCE_DESCRIPTOR:
            __CoreRunLoopSource_scheduleDescriptor(src, runLoop, mode);
            break;
        case CORE_RUN_LOOP_SOURCE_TIMER:
            break;
        default:
            break;
    }
    if (src->_delegate.schedule != NULL)
    {
        // notify
        src->_delegate.schedule(src->_delegate.info, runLoop, mode->_name);
    }
}


static void
__CoreRunLoopSource_activate(CoreRunLoopSourceRef src)
{
    if (src->_runLoop != NULL)
    {
        __CoreRunLoopSource_setValid(src, true);
    }
}

static void
__CoreRunLoopSource_deactivate(CoreRunLoopSourceRef src)
{
    __CoreRunLoopSource_setValid(src, false);
}


static void
__CoreRunLoopSource_removeFromRunLoop(CoreRunLoopSourceRef src)
{
    CoreRunLoopRef rl = src->_runLoop;
    CoreImmutableArrayRef modeNames = CoreRunLoop_getCopyOfModes(rl);
    CoreINT_U32 idx, n;
    
    n = CoreArray_getCount(modeNames);
    for (idx = 0; idx < n; idx++)
    {
        CoreImmutableStringRef name = CoreArray_getValueAtIndex(modeNames, idx);
        CoreRunLoop_removeSource(rl, src, name);
    }
    Core_release(modeNames);
}




static void
__CoreRunLoopSource_cleanup(CoreObjectRef o)
{
    CoreRunLoopSourceRef src = (CoreRunLoopSourceRef) o;
    
    CORE_DUMP_OBJ_TRACE(o, __FUNCTION__);
    if (src->_delegate.release != null)
    {
        src->_delegate.release(src->_delegate.info);
    }
}




static const CoreClass __CoreRunLoopSourceClass =
{
    0x00,                            // version
    "CoreRunLoopSource",             // name
    NULL,                            // init
    NULL,                            // copy
    __CoreRunLoopSource_cleanup,     // cleanup
    //__CoreRunLoopSource_equal,       // equal
    //__CoreRunLoopSource_hash,        // hash
    NULL,//__CoreRunLoopSource_getCopyOfDescription // getCopyOfDescription
};


static CoreRunLoopSourceRef
CoreRunLoopSource_create(
    CoreAllocatorRef allocator, 
    CoreRunLoopSourceType type,
    CoreRunLoopSourceDelegate * delegate
)
{
    CoreRunLoopSourceRef result = NULL;
    CoreINT_U32 size = sizeof(struct __CoreRunLoopSource);
    
    result = (CoreRunLoopSourceRef) 
        CoreRuntime_createObject(allocator, CoreRunLoopSourceID, size);
    if (result != NULL)
    {
        CORE_DUMP_MSG(
            CORE_LOG_TRACE | CORE_LOG_INFO, 
            "->%s: new object %p\n", __FUNCTION__, result
        );

        CoreSpinLock_init(&result->_lock);
        result->_runLoop = NULL;
        result->_delegate = NULL;
        memset(&result->_type, 0, sizeof(result->_type);
        __CoreRunLoopSource_setType(result, type);
        __CoreRunLoopSource_setValid(result, false);
        __CoreRunLoopSource_setSignaled(result, false);
        result->_delegate = { 0 };
        if (delegate != NULL)
        {
            result->_delegate = *delegate;
            if (delegate->retain != NULL)
            {
                delegate->retain(delegate->info);
            }
        }
        
        switch (type)
        {
            case CORE_RUN_LOOP_SOURCE_CUSTOM:
                result->_type._custom._timeout = timeout;
                result->_type._custom._priority = 0;
                __CoreRunLoopSource_setInitialized(src, true);
                break;
            case CORE_RUN_LOOP_SOURCE_DESCRIPTOR:
                result->_type._descriptor._timeout = timeout;
                result->_type._descriptor._priority = 0;
                result->_type._descriptor._descriptor = descriptor;
                __CoreRunLoopSource_setInitialized(src, true);
                break;
            case CORE_RUN_LOOP_SOURCE_TIMER:
            default:
                __CoreRunLoopSource_setInitialized(result, false);
                break;
        }
    }
    
    return result;
}

/* CORE_PUBLIC */ CoreRunLoopSourceRef
CoreRunLoopSource_createCustom(
    CoreAllocatorRef allocator, 
    CoreINT_U32 timeout,
    CoreRunLoopSourceDelegate * delegate
)
{
    CoreRunLoopSourceRef result;
    
    result = CoreRunLoopSource_create(
        allocator, CORE_RUN_LOOP_SOURCE_CUSTOM, delegate
    );
    if (result != NULL)
    {
        result->_type._custom._timeout = timeout;
        result->_type._custom._priority = 0;
        __CoreRunLoopSource_setInitialized(src, true);
    }
    
    return result;
}

/* CORE_PUBLIC */ CoreRunLoopSourceRef
CoreRunLoopSource_createDescriptor(
    CoreAllocatorRef allocator, 
    CoreSystemDescriptor descriptor,
    CoreINT_U32 timeout,
    CoreRunLoopSourceDelegate * delegate
)
{
    CoreRunLoopSourceRef result;
    
    result = CoreRunLoopSource_create(
        allocator, CORE_RUN_LOOP_SOURCE_DESCRIPTOR, delegate
    );
    if (result != NULL)
    {
        result->_type._descriptor._timeout = timeout;
        result->_type._descriptor._priority = 0;
        result->_type._descriptor._descriptor = descriptor;
        __CoreRunLoopSource_setInitialized(src, true);
    }
}

/* CORE_PUBLIC */ CoreRunLoopSourceRef
CoreRunLoopSource_createTimer(
    CoreAllocatorRef allocator,
    CoreINT_U32 delay,
    CoreINT_U32 period,
    CoreINT_U32 leeway
)
{
    CoreRunLoopSourceRef result;
    
    result = CoreRunLoopSource_create(
        allocator, CORE_RUN_LOOP_SOURCE_TIMER, delegate, -1, 0
    );
    if (result != NULL)
    {
        CoreRunLoopTimerSource * timer = &result->_type._timer;
        CoreINT_S64 now;
    
        now = Core_getCurrentTime_ms();
        timer->_fireTime = now + (CoreINT_S64) delay;
        timer->_period = period;
        timer->_leeway = leeway;
        __CoreRunLoopSource_setInitialized(src, true);
    }
    
    return result;
}     


/* CORE_PUBLIC */ CoreBOOL
CoreRunLoopSource_setDelegate(
    CoreRunLoopSourceRef src, CoreRunLoopSourceDelegate * delegate
)
{
    CoreBOOL result = false;
    
    CORE_DUMP_OBJ_TRACE(src, __FUNCTION__);
    if (src->_delegate.handler != NULL)
    {
        src->_delegate = *delegate;
        if (src->_delegate.retain != NULL)
        {
            src->_delegate.retain(src->_delegate.info);
        }
        result = true;
        
        switch (__CoreRunLoopSource_getType(src))
        {
            case CORE_RUN_LOOP_SOURCE_CUSTOM:
                __CoreRunLoopSource_setInitialized(src, true);
                break;
            default:
                break;
        }
    }
    
    return result;
}

/* CORE_PUBLIC */ void
CoreRunLoopSource_setTimer(
    CoreRunLoopSourceRef src, 
    CoreINT_U32 delay,
    CoreINT_U32 period,
    CoreINT_U32 leeway
)
{
    CoreRunLoopTimerSource * timer = &src->_type._timer;
    CoreINT_S64 now;

    CORE_DUMP_OBJ_TRACE(src, __FUNCTION__);
    CORE_ASSERT_RET0(
        __CoreRunLoopSource_getType(src) == CORE_RUN_LOOP_SOURCE_TIMER,
        CORE_LOG_ASSERT,
        "%s(): function called on non-timer source!",
        __PRETTY_FUNCTION__
    );

    now = Core_getCurrentTime_ms();
    timer->_fireTime = now + (CoreINT_S64) delay;
    timer->_period = period;
    timer->_leeway = leeway;
    __CoreRunLoopSource_setInitialized(src, true);
}

/* CORE_PUBLIC */ void
CoreRunLoopSource_setDescriptor(
    CoreRunLoopSourceRef src, CoreSystemDescriptor descriptor
)
{
    CORE_DUMP_OBJ_TRACE(src, __FUNCTION__);
    CORE_ASSERT_RET0(
        __CoreRunLoopSource_getType(src) == CORE_RUN_LOOP_SOURCE_DESCRIPTOR,
        CORE_LOG_ASSERT,
        "%s(): function called on non-descriptor source!",
        __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET0(
        __CoreSystemDescriptor_isValid(descriptor) == CORE_DESCRIPTOR_INVALID,
        CORE_LOG_ASSERT,
        "%s(): descriptor 0x%p is invalid!", descriptor
        __PRETTY_FUNCTION__
    );
    
    src->_type._system.descriptor = descriptor;
    __CoreRunLoopSource_setInitialized(src, true);
} 

/* CORE_PROTECTED */ CoreINT_S32 
CoreRunLoopSource_getPriority(CoreRunLoopSourceRef src)
{
    CoreINT_S32 result = 0;
    
    CORE_DUMP_OBJ_TRACE(src, __FUNCTION__);
    CORE_ASSERT_RET0(
        __CoreRunLoopSource_getType(src) == CORE_RUN_LOOP_SOURCE_DESCRIPTOR ||
        __CoreRunLoopSource_getType(src) == CORE_RUN_LOOP_SOURCE_CUSTOM,
        CORE_LOG_ASSERT,
        "%s(): function called on non-prioritizable source!",
        __PRETTY_FUNCTION__
    );
    
    switch (__CoreRunLoopSource_getType(src))
    {
        case CORE_RUN_LOOP_SOURCE_DESCRIPTOR:
            result = src->_type._descriptor._priority;
            break;
        case CORE_RUN_LOOP_SOURCE_CUSTOM:
            result = src->_type._custom._priority;
            break;
        default:
            break;
    }
    
    return result;
}

/* CORE_PROTECTED */ void
CoreRunLoopSource_setPriority(CoreRunLoopSourceRef src, CoreINT_S32 priority)
{
    CORE_DUMP_OBJ_TRACE(src, __FUNCTION__);
    CORE_ASSERT_RET0(
        __CoreRunLoopSource_getType(src) == CORE_RUN_LOOP_SOURCE_DESCRIPTOR ||
        __CoreRunLoopSource_getType(src) == CORE_RUN_LOOP_SOURCE_CUSTOM,
        CORE_LOG_ASSERT,
        "%s(): function called on non-prioritizable source!",
        __PRETTY_FUNCTION__
    );
    
    switch (__CoreRunLoopSource_getType(src))
    {
        case CORE_RUN_LOOP_SOURCE_SYSTEM:
            src->_type._descriptor._priority = priority;
            break;
        case CORE_RUN_LOOP_SOURCE_CUSTOM:
            src->_type._custom._priority = priority;
            break;
        default:
            break;
    }
}

/* CORE_PUBLIC */ CoreRunLoopRef 
CoreRunLoopSource_getRunLoop(CoreRunLoopSourceRef src)
{
    CoreRunLoopRef result;
    
    CORE_DUMP_OBJ_TRACE(src, __FUNCTION__);
    result = src->runLoop;
    
    return result;
}

/* CORE_PUBLIC */ void
CoreRunLoopSource_activate(CoreRunLoopSourceRef src)
{
    CORE_DUMP_OBJ_TRACE(src, __FUNCTION__);
    CORE_ASSERT_RET0(
        __CoreRunLoopSource_isInitialized(src),
        CORE_LOG_ASSERT,
        "%s(): cannot activate an uninitialized source!",
        __PRETTY_FUNCTION__
    );
    
    __CoreRunLoopSource_lock(src);
    __CoreRunLoopSource_activate(src);
    __CoreRunLoopSource_unlock(src);
}
    
/* CORE_PUBLIC */ void
CoreRunLoopSource_deactivate(CoreRunLoopSourceRef src)
{
    CORE_DUMP_OBJ_TRACE(src, __FUNCTION__);
    CORE_ASSERT_RET0(
        __CoreRunLoopSource_isInitialized(src),
        CORE_LOG_ASSERT,
        "%s(): cannot deactivate an uninitialized source!",
        __PRETTY_FUNCTION__
    );
    
    __CoreRunLoopSource_lock(src);
    __CoreRunLoopSource_deactivate(src);
    __CoreRunLoopSource_unlock(src);
}

/* CORE_PUBLIC */ void 
CoreRunLoopSource_signal(CoreRunLoopSourceRef src)
{
    CORE_DUMP_OBJ_TRACE(src, __FUNCTION__);
    
    __CoreRunLoopSource_lock(src);
    if (__CoreRunLoopSource_isValid(src))
    {
        __CoreRunLoopSource_setSignaled(src, true);
    }
    __CoreRunLoopSource_unlock(src);    
}







/*
 *
 *      CORE RUN LOOP MODE
 *
 */

struct __CoreRunLoopMode
{
    CoreRuntimeObject _core;
    CoreSpinLock _lock; // run loop must be locked before locking this
    CoreImmutableStringRef _name;
    CoreSetRef _sources;
//    CoreDictionaryRef _descriptors;
//    CoreSetRef _timers;
    CoreSetRef _observers; 
    CoreRunLoopActivity _observerMask;
#if defined(__LINUX__)
    int epollfd;
    CoreINT_U32 _descrCount;
#elif defined(__WIN32__)
    CoreDictionaryRef _descrMap;
#endif    
};



CORE_INLINE void 
__CoreRunLoopMode_lock(CoreRunLoopModeRef rlm)
{
    CoreSpinLock_lock(&rlm->_lock);    
}

CORE_INLINE void 
__CoreRunLoopMode_unlock(CoreRunLoopModeRef rlm)
{
    CoreSpinLock_unlock(&rlm->_lock);    
}


CORE_INLINE CoreBOOL
__CoreRunLoopMode_isEmpty(CoreRunLoopModeRef rlm)
{
    return (
        ((rlm->_sources == NULL) || (CoreSet_getCount(rlm->_sources) == 0)) &&
        ((rlm->_timers == NULL) || (CoreSet_getCount(rlm->_timers) == 0)) &&
        ((rlm->_descriptors == NULL) || (CoreDictionary_getCount(rlm->_descriptors) == 0))
    ) ? true : false;
}


static CoreBOOL
__CoreRunLoopMode_equal(CoreObjectRef o1, CoreObjectRef o2)
{
    CoreRunLoopModeRef rlm1 = (CoreRunLoopModeRef) o1;
    CoreRunLoopModeRef rlm2 = (CoreRunLoopModeRef) o2;
    return Core_equal(rlm1->_name, rlm2->_name);
}

static CoreHashCode
__CoreRunLoopMode_hash(CoreObjectRef o)
{
    CoreRunLoopModeRef rlm = (CoreRunLoopModeRef) o;
    return Core_hash(rlm->_name);
}

static CoreImmutableStringRef
__CoreRunLoopMode_getCopyOfDescription(CoreObjectRef o)
{
    return NULL; // TODO
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
    __CoreRunLoopMode_getCopyOfDescription // getCopyOfDescription
};








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
    CoreRunLoopActivity _activities;
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

CORE_INLINE CoreBOOL
__CoreRunLoopObserver_isSignaled(CoreRunLoopObserverRef rlo)
{
    return CoreBitfield_getValue(
        ((const CoreRuntimeObject *) rlo)->info,
        CORE_RUN_LOOP_OBSERVER_SIGNAL_BIT,
        1
    );
}

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
        result->_activities = activities;
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
#define CORE_RUN_LOOP_USE_SOCKETPAIR
#endif
  
struct __CoreRunLoop
{
    CoreRuntimeObject core;
    CoreSpinLock lock;  // for accesing modes; never try to lock with mode locked
    CoreSetRef modes;   // modes' names only
    CoreSetRef _currentModes;
    CoreSystemDescriptor _wakeupDescriptor;
#if defined(CORE_RUN_LOOP_USE_SOCKETPAIR)
    CoreSystemDescriptor _wakeupDescriptor2;         
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

CORE_INLINE void
__CoreRunLoop_lockModes(CoreRunLoopModeRef * modes)
{
    CoreINT_U32 idx;
    for (idx = 0; modes[idx] != NULL; idx++)
    {
        __CoreRunLoopMode_lock(modes[idx]);
    }
}

CORE_INLINE void
__CoreRunLoop_unlockModes(CoreRunLoopModeRef * modes)
{
    CoreINT_U32 idx;
    for (idx = 0; modes[idx] != NULL; idx++)
    {
        __CoreRunLoopMode_unlock(modes[idx]);
    }
}

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


//
// Returns true when all modes are empty.
//
CORE_INLINE CoreBOOL
__CoreRunLoop_emptyModes(CoreRunLoopModeRef * modes)
{
    CoreBOOL result = true;
    CoreINT_U32 idx;
    
    for (idx = 0; modes[idx] != NULL; idx++)
    {
        CoreRunLoopModeRef rlm = modes[idx];
        result = result && __CoreRunLoopMode_isEmpty(rlm);
    }
    
    return result;
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
    tmp._name = modeName;
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
            result->_sources = null;
            result->_timers = null;
            result->_observers = null;
            result->_observerMask = 0;
            CoreSpinLock_init(&result->_lock);
            result->_name = Core_retain(modeName);
            result->_descriptors = CoreDictionary_create(
                Core_getAllocator(rl), 0, NULL, &CoreDictionaryValueCoreCallbacks
            );
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
    (void) send(rl->_wakeupDescriptor2, &c, sizeof(c), 0);    
}
#elif defined(__WIN32__)
static void 
__CoreRunLoop_wakeUp(CoreRunLoopRef rl)
{
    /*fprintf(stdout, "rl %p triggering wake up\n", rl);
    fflush(stdout);*/
    SetEvent(rl->_wakeupDescriptor);
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

static CoreTimerRef 
__CoreRunLoop_getMinTimer(CoreRunLoopRef rl, CoreRunLoopModeRef * modes)
{
    CoreTimerRef result = NULL;
    CoreINT_U32 idx;
    
    for (idx = 0; modes[idx] != NULL; idx++)
    {
        CoreRunLoopModeRef rlm = modes[idx];
        if ((rlm->_timers != null) && (CoreSet_getCount(rlm->_timers) > 0))
        {
            CoreSet_applyFunction(
                rlm->_timers, 
                __CoreRunLoop_findMinTimer,
                &result
            );
        }
    }
    
    return result;
}


static CoreSystemDescriptor *
__CoreRunLoop_collectDescriptors(
    CoreRunLoopRef rl, CoreRunLoopModeRef * modes,
    CoreSystemDescriptor * buffer, CoreINT_U32 * descrCount
)
{
    CoreSystemDescriptor * result = buffer;
    CoreINT_U32 count = 0;
    CoreINT_U32 idx;
    
    for (idx = 0; modes[idx] != NULL; idx++)
    {
        CoreRunLoopModeRef rlm = modes[idx];
        if (rlm->_descriptors != NULL)
        {
            count += CoreDictionary_getCount(rlm->_descriptors);
        }
    }
    count++; // +1 for wakeup descriptor
    if (count > *descrCount)
    {
        result = CoreAllocator_allocate(
            Core_getAllocator(rl), count * sizeof(CoreSystemDescriptor)
        );
    }
    if ((count > 0) && (result != NULL))
    {
        CoreINT_U32 k = 0;
        
        for (idx = 0; modes[idx] != NULL; idx++)
        {
            CoreRunLoopModeRef rlm = modes[idx];
            if (rlm->_descriptors != NULL)
            {
                CoreDictionary_copyKeysAndValues(
                    rlm->_descriptors, result + k, NULL
                );
                k += CoreDictionary_getCount(rlm->_descriptors);
            }            
        }
        result[k] = rl->_wakeupDescriptor;
    }
    
    *descrCount = count;
    
    return result;
}


#if defined(__LINUX__)

// The rlm's epollfd contains already signaled descriptors, so calling 
// epoll_wait() shouldn't put us to sleep.  
//
// rlm is locked
static void
__CoreRunLoopMode_getSignaledDescriptors(
    CoreRunLoopModeRef rlm,
    CoreBOOL oneshot,
    CoreObjectRef * signaled
)
{
    
    struct epoll_event eventBuffer[(oneshot) ? 1 : CORE_MAX_STACK_DESCRIPTORS];
    struct epoll_event * events = eventBuffer;
    
    if (!oneshot && (rlm->_epollCount > CORE_MAX_STACK_DESCRIPTORS))
    {
        events = CoreAllocator_allocate(
            Core_getAllocator(rlm), rlm->_epollCount * sizeof(int)
        );
    }
    if (events != NULL)
    {
        int result;
        
        do
        {
            result = epoll_wait(rlm->_epollfd, events, 1, 0);
        }
        while ((result < 0) && (errno == EINTR));
        
        if (result > 0)
        {
            if (oneshot)
            {
                CoreRunLoopSourceRef src = events[0].data.ptr;
                *signaled = Core_retain(src); 
            }
            else
            {
                CoreArrayRef array = NULL;
                
                if (*signaled == NULL)
                {
                    array = CoreArray_create(
                        CORE_ALLOCATOR_SYSTEM, 0, &CoreArrayCoreCallbacks
                    );
                }
                if (array != NULL)
                {
                    for (idx = 0; idx < result; idx++)
                    {
                        CoreRunLoopSourceRef src = events[idx].data.ptr;
                        CoreArray_addValue(array, src);
                    }
                    *signaled = array;
                }
            }
        }
    }
}

// rlm is locked on entry and exit
static CoreINT_S32
__CoreRunLoop_waitForEvent_epoll(
    CoreRunLoopRef rl,
    CoreRunLoopModeRef * modes,
    CoreINT_S64 timeout,
    CoreBOOL oneshot,
    CoreObjectRef * signaled
)
{
    CoreINT_S32 result = 0;
    CoreINT_U32 idx;
    CoreINT_U32 count = 0;
    struct epoll_event ev;
    
    if (rl->_epollfd == -1)
    {
        rl->_epollfd = epoll_create(8);
        
        // add wakeupDescriptor after initialization
        if (rl->_epollfd != -1)
        {
            CoreSystemDescriptor wakeUpDescriptor;
            
            ev.events = EPOLLIN;
            ev.data.ptr = rl->_wakeupSource;
            wakeUpDescriptor = CoreRunLoopSource_getDescriptor(rl->_wakeupSource);
            epoll_ctl(r->_epollfd, EPOLL_CTL_ADD, wakeUpDescriptor, &ev);
            count++;
        }
    }
    if (rl->_epollfd != -1)
    {
        for (idx = 0; modes[idx] != NULL; idx++)
        {
            CoreRunLoopModeRef rlm = modes[idx];
            if (rlm->_epollfd != -1)
            {
                ev.events = EPOLLIN;
                ev.data.ptr = rlm;
                epoll_ctl(r->_epollfd, EPOLL_CTL_ADD, rlm->_epollfd, &ev);
                count++;       
            }
        }
    }
    
    // We are going to sleep if either timeout is > 0 or we have some
    // descriptors to check except the wakeupDescriptor.
    if ((timeout > 0) || (count > 1))
    {
        struct epoll_event events[CORE_RUN_LOOP_MAX_MODES + 1];

        __CoreRunLoop_unlockModes(modes);
        do
        {
            result = epoll_wait(
                rl->_epollfd, 
                events, 
                (oneshot) ? 1 : count, 
                (CoreINT_S32) timeout
            );
        }    
        while (result == -EINTR);
        __CoreRunLoop_lockModes(modes);
        
        if (result > 0)
        {
            for (idx = 0; idx < result; idx++)
            {
                if (events[idx].data.ptr == rl->_wakeupSource)
                {
                    __CoreRunLoop_handleSource(rl, rl->_wakeupSource);
                    result = ...;
                }
                else
                {
                    CoreRunLoopModeRef rlm = events[idx].data.ptr;
                    __CoreRunLoopMode_getSignaledDescriptors(
                        rlm,
                        oneshot,
                        signaled
                    );
                    if (oneshot)
                    {
                        break;
                    }
                }
            }
        }
    }
    
    // Now remove all modes' epollfd.
    for (idx = 0; modes[idx] != NULL; idx++)
    {
        CoreRunLoopModeRef rlm = modes[idx];
        if (rlm->_epollfd != -1)
        {
            epoll_ctl(r->_epollfd, EPOLL_CTL_DEL, rlm->_epollfd, &ev);
            count++;       
        }
    }
    
    return result;    
} 
    

static CoreINT_S32
__CoreRunLoop_waitForEvent_epoll2(
    CoreRunLoopRef rl, 
    CoreRunLoopModeRef * modes,
    CoreINT_S64 timeout,
    CoreSystemDescriptor * signalled 
)
{
    CoreINT_S32 result;
    CoreINT_U32 idx;
    int epollfdall = epoll_create(CORE_RUN_LOOP_MAX_MODES);
    
    if (epollfdall != -1)
    {
        for (idx = 0; modes[idx] != NULL; idx++)
        {
            CoreRunLoopModeRef rlm = modes[idx];
            if (rlm->_epollfd != -1)
            {
                struct epoll_event ev;
                
                ev.events = EPOLLIN;
                ev.data.fd = descriptors[idx];
                epoll_ctl(epollfdall, EPOLL_CTL_ADD, rlm->_epollfd, &ev);
            }
        }
        
        do
        {
            // we are satisfied with 1 descriptor only
            result = epoll_wait(
                epollfdall, events, 1, (CoreINT_S32) timeout
            );
        }    
        while (result == -EINTR);
    }
    
    if (result > 0)
    {
        if (signalled != NULL)
        {
            *signalled = events[idx].data.fd;
        }
        
        // drain the wakeupDescriptor pipe
        if (events[idx].data.fd == rl->_wakeupDescriptor)
        {
            int cnt;
            do
            {
                char buff[32] = {0};
                cnt = recv(rl->_wakeupDescriptor, buff, 32, 0);
            }
            while (cnt > 0);
        }
    }
    else if (result == 0)
    {
        result = CORE_RUN_LOOP_WAIT_TIMEOUT;
    }
    else 
    {
        result = CORE_RUN_LOOP_WAIT_ERROR;
        CORE_DUMP_MSG(
            CORE_LOG_INFO | CORE_LOG_CRITICAL, 
            "RunLoop %p: error from poll: %d errno\n", rl, errno
        );
    }
    
    close(epollfd);
    
    if (events != eventBuffer)
    {
        CoreAllocator_deallocate(Core_getAllocator(rl), events);
    }
    
    return result;    
}

static CoreINT_S32
__CoreRunLoop_waitForEvent_poll(
    CoreRunLoopRef rl, 
    CoreSystemDescriptor * descriptors,
    CoreINT_U32 count,
    CoreINT_S64 timeout,
    CoreSystemDescriptor * signalled 
)
{
    CoreINT_S32 result;
    CoreINT_U32 idx;
    CoreBOOL wokenup = false;
    struct pollfd fdsbuffer[CORE_MAX_STACK_DESCRIPTORS];
    struct pollfd * fds = fdsbuffer;
    
    if (count > CORE_MAX_STACK_DESCRIPTORS)
    {
        fds = CoreAllocator_allocate(
            Core_getAllocator(rl), count * sizeof(struct pollfd)
        );
    }
            
    for (idx = 0; idx < count; idx++)
    {
        fds[idx].fd = descriptors[idx];
        fds[idx].events = POLLIN;
    }
    
    do
    {
        // we are satisfied with 1 descriptor only
        result = poll(fds, count, (CoreINT_S32) timeout);
    }    
    while (result == -EINTR);
    
    if (result > 0)
    {
        int cnt;
        
        for (idx = 0; idx < count; idx++)
        {
            if (fds[idx].revents & POLLIN)
            {
                if (fds[idx].fd == rl->_wakeupDescriptor)
                {
                    // drain the wakeupDescriptor pipe
                    int cnt;
                    
                    do
                    {
                        char buff[32] = {0};
                        cnt = recv(rl->_wakeupDescriptor, buff, 32, 0);
                    }
                    while (cnt > 0);
                }
                else if (signalled != NULL)
                {
                    *signalled = fds[idx].fd;
                }
                else { }
            }
            break;
        }
        
    }
    else if (result == 0)
    {
        result = CORE_RUN_LOOP_WAIT_TIMEOUT;
    }
    else 
    {
        result = CORE_RUN_LOOP_WAIT_ERROR;
        CORE_DUMP_MSG(
            CORE_LOG_INFO | CORE_LOG_CRITICAL, 
            "RunLoop %p: error from poll: %d errno\n", rl, errno
        );
    }
    
    if (fds != fdsbuffer)
    {
        CoreAllocator_deallocate(Core_getAllocator(rl), fds);
    }
    
    return result;    
}

#elif defined(__WIN32__)
static CoreINT_S32
__CoreRunLoop_waitForEvent_win32(
    CoreRunLoopRef rl, 
    CoreSystemDescriptor * descriptors,
    CoreINT_U32 count,
    CoreINT_S64 timeout,
    CoreSystemDescriptor * signalled 
)
{
    CoreINT_S32 result;
    DWORD waitResult;
    
    waitResult = MsgWaitForMultipleObjects(
        min(count, MAXIMUM_WAIT_OBJECTS), 
        descriptors, 
        false, 
        (DWORD) timeout, 
        0
    );
    ResetEvent(rl->_wakeupDescriptor);
    
    if ((waitResult >= WAIT_OBJECT_0) && (waitResult < count))
    {
        result = (CoreINT_S32) (waitResult - WAIT_OBJECT_0);
        if (signalled != NULL)
        {
            *signalled = descriptors[waitResult];
        }
    }
    else if (waitResult == WAIT_TIMEOUT)
    {
        result = CORE_RUN_LOOP_WAIT_TIMEOUT;
    }
    else
    {
        result = CORE_RUN_LOOP_WAIT_ERROR;
        CORE_DUMP_MSG(
            CORE_LOG_INFO | CORE_LOG_CRITICAL, 
            "RunLoop %p: Unexpected result from MsgWaitForMultipleObjects: %d\n", 
            rl, waitResult
        );
    }
    
        
    return result;
}
#endif 


// modes are locked on entry and exit
static CoreINT_S32
__CoreRunLoop_waitForEvent(
    CoreRunLoopRef rl, CoreRunLoopModeRef * modes, 
    CoreINT_S64 timeout, CoreSystemDescriptor * signalled
)
{
    CoreINT_S32 result = 0;
    CoreINT_S64 sleepTime = 0;
    CoreRunLoopSourceRef minTimeSrc = NULL;
    CoreINT_S64 now_ms = Core_getCurrentTime_ms();
    
    minTimeSrc = __CoreRunLoop_getMinTimeoutSrc(rl, modes, now);
    if (minTimeSrc != NULL)
    {
        switch (__CoreRunLoopSource_getType(minTimeSrc))
        {
            case CORE_RUN_LOOP_SOURCE_CUSTOM:
                fireTime = minTimeSrc->_type._custom._timeout;
                break;
            case CORE_RUN_LOOP_SOURCE_DESCRIPTOR:
                fireTime = minTimeSrc->_type._descriptor._timeout;
                break;
            case CORE_RUN_LOOP_SOURCE_TIMER:
                fireTime = minTimeSrc->_type._timer._fireTime;
                break;
            default:
                break;
        }
    }
    
    // when zero timeout, set the result, however continue in computing --
    // fired timers can override this result
    timeout = min(timeout, CoreINT_S32_MAX);
    if (timeout == 0)
    {
        result = CORE_RUN_LOOP_WAIT_TIMEOUT;
    }
    
    if (fireTime >= 0)
    {
        CoreINT_S64 now_ms = Core_getCurrentTime_ms();

        if (fireTime <= now_ms)
        {
            result = CORE_RUN_LOOP_WAIT_TIMER_FIRED; // sleepTime remains 0
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
    
    result = __CoreRunLoop_waitForEvent_sys(rl, sleepTime, &signaled);

    return result;
}


// modes are locked on entry and exit
static CoreINT_S32
__CoreRunLoop_waitForEvent2(
    CoreRunLoopRef rl, CoreRunLoopModeRef * modes, 
    CoreINT_S64 timeout, CoreSystemDescriptor * signalled
)
{
    CoreINT_S32 result = 0;
    CoreINT_S64 sleepTime = 0;
    CoreTimerRef minTimer = NULL;
    CoreINT_S64 fireTime = -1;
    CoreSystemDescriptor descrBuffer[CORE_MAX_STACK_DESCRIPTORS];
    CoreSystemDescriptor * descriptors;
    CoreINT_U32 descrCount = 0;
    CoreINT_U32 idx;
    
    // descriptors can point to a different place after this...
    descriptors = __CoreRunLoop_collectDescriptors(
        rl, modes, descrBuffer, &descrCount
    );
    
    minTimer = __CoreRunLoop_getMinTimer(rl, modes);
    if (minTimer != NULL)
    {
        fireTime = minTimer->fireTime;
    }

    // when zero timeout, set the result, however continue in computing --
    // fired timers can override this result
    timeout = min(timeout, CoreINT_S32_MAX);
    if (timeout == 0)
    {
        result = CORE_RUN_LOOP_WAIT_TIMEOUT;
    }
    
    if (fireTime >= 0)
    {
        CoreINT_S64 now_ms = Core_getCurrentTime_ms();

        if (fireTime <= now_ms)
        {
            result = CORE_RUN_LOOP_WAIT_TIMER_FIRED; // sleepTime remains 0
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
    
    // We are going to sleep if either sleepTime is > 0 or we have some
    // descriptors to check except the wakeupDescriptor.
    if ((sleepTime > 0) || (descrCount > 1))
    {
        CORE_DUMP_MSG(
            CORE_LOG_INFO, "RunLoop %p going to sleep for %dms\n", rl, sleepTime
        );
        
        // go to sleep...
        __CoreRunLoop_unlockModes(modes); // unlock before waiting
        result = __CoreRunLoop_waitForEvent_sys(
            rl, descriptors, descrCount, sleepTime, signalled
        );
       
        CORE_DUMP_MSG(CORE_LOG_INFO, "RunLoop %p woken up with res %d\n", rl);
        
        // Now set the result for TIMEOUT to appropriate reason.   
        if ((result == CORE_RUN_LOOP_WAIT_TIMEOUT) && 
            ((sleepTime != timeout) || (fireTime == timeout)))
        {
            result = CORE_RUN_LOOP_WAIT_TIMER_FIRED;
        }
        
        __CoreRunLoop_lockModes(modes);
    }
        
    return result;
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


static void __CoreRunLoop_doTimer(
    CoreRunLoopRef rl, CoreRunLoopModeRef rlm, CoreTimerRef rlt, CoreINT_S64 now
)
{
     __CoreTimer_lock(rlt);
     __CoreTimer_setSignaled(rlt, false);
     if (__CoreTimer_isValid(rlt))
     {
         __CoreTimer_unlock(rlt);
         if (rlt->callback != null)
         {
             rlt->callback(rlt, rlt->userInfo.info); // callout
         }
         
         // check periodicity... possible rescheduling
         if (rlt->period == 0)
         {
             CoreTimer_cancel(rlt);
         }
         else
         {
             // Now the question -- should we set the next fireTime according
             // to scheduled fire time or according to actual fired time?
             
             //rt->fireTime = now + (CoreINT_S64) rt->period; // actual
             while (rlt->fireTime <= now) // scheduled
             {
                rlt->fireTime += (CoreINT_S64) rlt->period;
             }
         }
     }
     else
     {
         __CoreTimer_unlock(rlt);
     }
}

// mode is locked on entry and exit
static CoreBOOL
__CoreRunLoop_doTimersInMode(
    CoreRunLoopRef rl, CoreRunLoopModeRef rlm, CoreINT_S64 now)
{
    CoreBOOL result = false;
    CoreINT_U32 count = 0;
    CoreTimerRef buffer[32];
    CoreTimerRef * timers = buffer;

    count = CoreSet_getCount(rlm->_timers);
    if (count > 32)
    {
        timers = CoreAllocator_allocate(
            Core_getAllocator(rlm), count * sizeof(CoreTimerRef)
        );
    }
    if ((count > 0) && (timers != NULL))
    {
        CoreINT_U32 idx = 0;
        
        CoreSet_copyValues(rlm->_timers, timers);
        for (idx = 0; idx < count; idx++)
        {
            CoreTimerRef timer = timers[idx];
            if (__CoreTimer_isValid(timer) && (timer->fireTime <= now))
            {
                Core_retain(timer);
            }
            else
            {
                timers[idx] = NULL;
            }
        }
        
        __CoreRunLoopMode_unlock(rlm); // unlock before callouts
        
        for (idx = 0; idx < count; idx++)
        {
            CoreTimerRef timer = timers[idx];
            if (timer != NULL)
            {
                __CoreRunLoop_doTimer(rl, rlm, timer, now);
                Core_release(timer);
            }
        }
        
        if (timers != buffer)
        {
            CoreAllocator_deallocate(Core_getAllocator(rlm), timers);
        }
        
        __CoreRunLoopMode_lock(rlm);
    }
    
    return result;
}

// modes are locked on entry and exit
static CoreBOOL
__CoreRunLoop_doTimers(CoreRunLoopRef rl, CoreRunLoopModeRef * modes)
{
    CoreBOOL result = false;
    CoreINT_U32 idx = 0;
    CoreINT_S64 now = Core_getCurrentTime_ms();
    
    for (idx = 0; modes[idx] != NULL; idx++)
    {
        CoreRunLoopModeRef rlm = modes[idx];
		if (rlm->_timers != NULL)
		{
			result = __CoreRunLoop_doTimersInMode(rl, rlm, now);
		}
    }
    
    return result;
}


// rlm locked on entry and exit
static void __CoreRunLoop_doSystemSource(
    CoreRunLoopRef rl, CoreRunLoopModeRef rlm, CoreRunLoopSourceRef rls
)
{
    Core_retain(rls);
    __CoreRunLoopMode_unlock(rlm);
    __CoreRunLoopSource_lock(rls);
    __CoreRunLoopSource_setSignaled(rls, false);
    if (__CoreRunLoopSource_isValid(rls))
    {
        __CoreRunLoopSource_unlock(rls);
        if (rls->delegate.perform != null)
        {
            rls->delegate.perform(rls->delegate.info); // callout
        }
    }
    Core_release(rls);
    __CoreRunLoopMode_lock(rlm);
}


static CoreRunLoopSourceRef
__CoreRunLoop_getSourceForDescriptor(
    CoreRunLoopModeRef * modes, 
    CoreSystemDescriptor descriptor,
    CoreRunLoopModeRef * foundMode
)
{
    CoreRunLoopSourceRef result = NULL;
    CoreINT_U32 idx;
    
    for (idx = 0; modes[idx] != NULL; idx++)
    {
        CoreRunLoopModeRef rlm = modes[idx];
        if (rlm->_descriptors != NULL)
		{
			result = CoreDictionary_getValue(rlm->_descriptors, descriptor);
			if (result != NULL)
			{
				*foundMode = rlm;
				break;             
			}
		}
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

// mode is locked on entry and exit
static CoreBOOL
__CoreRunLoop_doCustomSources(
    CoreRunLoopRef rl, CoreRunLoopModeRef * modes, CoreBOOL oneshot)
{
    CoreBOOL result = false;
    CoreObjectRef sources = null;
    CoreINT_U32 idx;
    
    for (idx = 0; modes[idx] != NULL; idx++)
    {
        CoreRunLoopModeRef rlm = modes[idx];
        if ((rlm->_sources != null) && (CoreSet_getCount(rlm->_sources) > 0))
        {
            CoreSet_applyFunction(
                rlm->_sources, __CoreRunLoop_collectSources, &sources
            );
        }
    }

    if (sources != null)
    {
        __CoreRunLoop_unlockModes(modes);
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
                    src->delegate.perform(src->delegate.info); // callout
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
                        src->delegate.perform(src->delegate.info); // callout
                    }
                    result = true;
                    if (oneshot)
                    {
                        break;
                    }
                }
                else
                {
                    __CoreRunLoopSource_unlock(src);
                }
            }
        }
        Core_release(sources);
        __CoreRunLoop_lockModes(modes);         
    }
    
    return result;
}


// mode is locked on entry and exit
static void
__CoreRunLoop_doObserversInMode(
    CoreRunLoopRef rl, 
    CoreRunLoopModeRef rlm, 
    CoreRunLoopActivity activity
)
{
    CoreINT_U32 count = 0;
    CoreRunLoopObserverRef buffer[32];
    CoreRunLoopObserverRef * observers = buffer;

    count = CoreSet_getCount(rlm->_observers);
    if (count > 32)
    {
        observers = CoreAllocator_allocate(
            Core_getAllocator(rlm), count * sizeof(CoreRunLoopObserverRef)
        );
    }
    if ((count > 0) && (observers != NULL))
    {
        CoreINT_U32 idx = 0;
        
        CoreSet_copyValues(rlm->_observers, observers);
        for (idx = 0; idx < count; idx++)
        {
            CoreRunLoopObserverRef rlo = observers[idx];
            if ((rlo->_activities & activity) &&
                (__CoreRunLoopObserver_isValid(rlo)))
            {
                Core_retain(rlo);
            }
            else
            {
                observers[idx] = NULL;
            }
        }
        
        __CoreRunLoopMode_unlock(rlm); // unlock before callouts
        
        for (idx = 0; idx < count; idx++)
        {
            CoreRunLoopObserverRef rlo = observers[idx];
            if (rlo != NULL)
            {
                rlo->callback(rlo, activity, rlo->userInfo.info); // callout
                Core_release(rlo);
            }
        }
        
        if (observers != buffer)
        {
            CoreAllocator_deallocate(Core_getAllocator(rlm), observers);
        }
        
        __CoreRunLoopMode_lock(rlm);
    }
}

// modes are locked on entry and exit
static void
__CoreRunLoop_doObservers(
    CoreRunLoopRef rl, 
    CoreRunLoopModeRef * modes, 
    CoreRunLoopActivity activity
)
{
    CoreINT_U32 idx = 0;

    for (idx = 0; modes[idx] != NULL; idx++)
    {
        CoreRunLoopModeRef rlm = modes[idx];
        if ((rlm->_observers != NULL) && (rlm->_observerMask & activity))
        {
            __CoreRunLoop_doObserversInMode(rl, rlm, activity);
        }
    }
}


/* mode is locked */
static CoreRunLoopResult 
__CoreRunLoop_runInModes(
    CoreRunLoopRef rl,
    CoreRunLoopModeRef * modes,
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
        CoreINT_S32 waitResult = 0; // TODO
        CoreINT_S64 timeout = time;
        CoreRunLoopSourceRef descrSource = NULL;
        CoreSystemDescriptor signalled = CORE_SYSTEM_DESCRIPTOR_INVALID;
        
        //
        // Dispatch all custom sources first...
        //
        __CoreRunLoop_doObservers(rl, modes, CORE_RUN_LOOP_BEFORE_SOURCES);
        sourceHandled = __CoreRunLoop_doCustomSources(rl, modes, returnAfterHandle);

        if (sourceHandled && returnAfterHandle)
        {
            poll = true;
            timeout = 0; // we just check timers and system sources
        }
        
        // The platform-dependent waiting...
        if (!poll)
        {
            __CoreRunLoop_doObservers(rl, modes, CORE_RUN_LOOP_BEFORE_WAITING);
            __CoreRunLoop_setSleeping(rl, true);
        }
        waitResult = __CoreRunLoop_waitForEvent(
            rl, modes, timeout, &signalled
        );
        if (!poll)
        {
            __CoreRunLoop_setSleeping(rl, false);
            __CoreRunLoop_doObservers(rl, modes, CORE_RUN_LOOP_AFTER_WAITING);
        }
        
        //
        // Dispatch all system sources...
        //
        if (waitResult >= 0)
        {
            CoreRunLoopModeRef foundMode = NULL;
            
            // we know the index of the signaled descriptor...
            //CoreSystemDescriptor d = descriptors[waitResult];
            
            if (signalled != CORE_SYSTEM_DESCRIPTOR_INVALID)
            {
                descrSource = __CoreRunLoop_getSourceForDescriptor(
                    modes, signalled, &foundMode
                );
                sourceHandled = true;
            }
            if (descrSource != NULL)
            {
                __CoreRunLoop_doSystemSource(rl, foundMode, descrSource);
            }
        }
        
        // 
        // Dispatch all fired timers here... (after switching timers to 
        // system sources, we can skip this)
        //
        (void) __CoreRunLoop_doTimers(rl, modes);
        
        poll = false;
            
        //    
        // Evaluate the result...
        //
        if (sourceHandled && returnAfterHandle)
        {
            result = CORE_RUN_LOOP_SOURCE_HANDLED;
        }
        else if ((time <= 0) || (waitResult == CORE_RUN_LOOP_WAIT_TIMEOUT))
        {
            result = CORE_RUN_LOOP_TIMED_OUT;
        }
        else if (__CoreRunLoop_isStopped(rl))
        {
            result = CORE_RUN_LOOP_STOPPED;
        }
        else if (__CoreRunLoop_emptyModes(modes))
        {
            result = CORE_RUN_LOOP_FINISHED;
        }
        else
        {
            // e.g. CORE_RUN_LOOP_WAIT_TIMER_FIRED... run another loop
        }
    } // while (result == 0)
    
    return result;
}


static CoreRunLoopResult 
__CoreRunLoop_run(
    CoreRunLoopRef rl,
    CoreImmutableStringRef * modes, // NULL terminating array
    CoreINT_S64 time,
    CoreBOOL returnAfterHandle
)
{
    CoreINT_U32 result = CORE_RUN_LOOP_FINISHED;
    CoreRunLoopModeRef rlms[CORE_RUN_LOOP_RUN_MAX_MODES + 1] = { NULL };
    CoreINT_U32 idx, k;
    CoreBOOL hasModes = false;
    CoreBOOL empty = true;

    __CoreRunLoop_lock(rl);
    CoreSet_clear(rl->_currentModes);
    for(idx = 0, k = 0; modes[idx] != NULL; idx++)
    {
        CoreRunLoopModeRef rlm;
        
        rlm = __CoreRunLoop_findMode(rl, modes[idx], false);
        if (rlm != NULL)
        {
            __CoreRunLoopMode_lock(rlm);
            CoreSet_addValue(rl->_currentModes, rlm);
            empty = empty && __CoreRunLoopMode_isEmpty(rlm);
            hasModes = true;
        }
        rlms[k++] = rlm;
    }
    
    if (hasModes)
    {
        if (!empty || (time > 0))
        {
            __CoreRunLoop_unlock(rl);
            __CoreRunLoop_doObservers(rl, rlms, CORE_RUN_LOOP_ENTRY);
            result = __CoreRunLoop_runInModes(rl, rlms, time, returnAfterHandle);
            __CoreRunLoop_doObservers(rl, rlms, CORE_RUN_LOOP_EXIT);
        }
        else
        {
            __CoreRunLoop_unlock(rl);
        }
        __CoreRunLoop_unlockModes(rlms);
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

        #if defined(__LINUX__)
        {
            SOCKET selfpipe[2];
            int ok;
            
            ok = socketpair(PF_LOCAL, SOCK_DGRAM, 0, selfpipe);
            if (ok == 0)
            {
                int oldFlag = fcntl(selfpipe[0], F_GETFL, 0);
                
                fcntl(selfpipe[0], F_SETFL, oldFlag | O_NONBLOCK);
                fcntl(selfpipe[1], F_SETFL, oldFlag | O_NONBLOCK);
                result->_wakeupDescriptor = selfpipe[0];
                result->_wakeupDescriptor2 = selfpipe[1];
            }
        }
        #elif defined(__WIN32__)
        result->_wakeupDescriptor = CreateEvent(NULL, true, false, NULL);
        #endif
        
        result->modes = CoreSet_create(
            CORE_ALLOCATOR_SYSTEM, 0, &CoreSetValueCoreCallbacks
        );
        result->_currentModes = CoreSet_create(
            CORE_ALLOCATOR_SYSTEM, 0, &CoreSetValueNullCallbacks
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

    count = CoreSet_getCount(mode->_sources);
    if ((mode->_sources != null) && (count > 0))
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
        CoreSet_copyValues(mode->_sources, sources);
        
        // Retain first
        for (idx = 0; idx < count; idx++)
        {
            Core_retain(sources[idx]);
        }
        
        // Now clear the sources in mode
        CoreSet_clear(mode->_sources);
        
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

    count = CoreSet_getCount(mode->_timers);
    if ((mode->_timers != null) && (count > 0))
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
        CoreSet_copyValues(mode->_timers, timers);
        
        // Retain first
        for (idx = 0; idx < count; idx++)
        {
            Core_retain(timers[idx]);
        }
        
        // Now clear the timers in mode
        CoreSet_clear(mode->_timers);
        
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

    count = CoreSet_getCount(mode->_observers);
    if ((mode->_observers != null) && (count > 0))
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
        CoreSet_copyValues(mode->_observers, observers);
        
        // Retain first
        for (idx = 0; idx < count; idx++)
        {
            Core_retain(observers[idx]);
        }
        
        // Now clear the observers in mode
        CoreSet_clear(mode->_observers);
        
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
    if (rl->_currentModes != NULL)
    {
        rl->_currentModes = null;
    }
#if defined(__WIN32__)    
    CloseHandle(rl->_wakeupDescriptor);
#elif defined(__LINUX__)
    close(rl->_wakeupDescriptor);
    close(rl->_wakeupDescriptor2);
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
        CoreImmutableStringRef modes[2] = { CORE_RUN_LOOP_MODE_DEFAULT, NULL };
        
        do
        {
            result = __CoreRunLoop_run(
                runLoop,
                modes,
                CoreINT_S64_MAX,
                false
            );
        }
        while ((result != CORE_RUN_LOOP_STOPPED) 
               || (result != CORE_RUN_LOOP_FINISHED));
    }
}

/* CORE_PROTECTED */ CoreRunLoopResult
_CoreRunLoop_runInModes(
    CoreImmutableStringRef * modes,
    CoreINT_S64 delay,
    CoreBOOL returnAfterHandle)
{
    return __CoreRunLoop_run(runLoop, modeBuf, delay, returnAfterHandle);
}


/* CORE_PUBLIC */ CoreRunLoopResult
CoreRunLoop_runInModes(
    CoreImmutableArrayRef modes,
    CoreINT_S64 delay,
    CoreBOOL returnAfterHandle)
{
    CoreRunLoopResult result = 0;
    CoreRunLoopRef runLoop;
    
    CORE_DUMP_TRACE(__FUNCTION__);

    runLoop = __CoreRunLoop_getRunLoop();
    if (runLoop != null)
    {
        // CORE_RUN_LOOP_RUN_MAX_MODES + 1 terminating
        CoreImmutableStringRef modeBuf[CORE_RUN_LOOP_RUN_MAX_MODES + 1] = { NULL };
        CoreINT_U32 countOfModes = CoreArray_getCount(modes);
        
        if (countOfModes <= CORE_RUN_LOOP_RUN_MAX_MODES)
        {
            CoreArray_copyValues(modes, CoreRange_make(0, countOfModes), modeBuf);
            result = __CoreRunLoop_run(
                runLoop, modeBuf, delay, returnAfterHandle
            );
        }
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


/*static CoreImmutableStringRef
__CoreRunLoop_getCurrentModeName_nolock(CoreRunLoopRef rl)
{
    return rl->currentmode->_name;
} */

/* CORE_PUBLIC */ CoreImmutableStringRef 
CoreRunLoop_getCurrentModeName(CoreRunLoopRef rl)
{
    CoreImmutableStringRef result = null;
    
    CORE_DUMP_OBJ_TRACE(rl, __FUNCTION__);

    __CoreRunLoop_lock(rl);
//    result = rl->currentmode->_name; // TODO
    __CoreRunLoop_unlock(rl);
    
    return result;
}


/* CORE_PUBLIC */ void // TODO
CoreRunLoop_addMode(
    CoreRunLoopRef rl,
    CoreImmutableStringRef modeName,
    CoreImmutableStringRef toModeName
)
{
    CoreRunLoopModeRef rlm = null;
    
    CORE_DUMP_OBJ_TRACE(rl, __FUNCTION__);

    /* check run loop state...*/
/*    if (__CoreRunLoop_isDeallocating(rl)) return;
    
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
    }*/
}


/* CORE_PUBLIC */ void // TODO
CoreRunLoop_removeSubmodeFromMode(
    CoreRunLoopRef rl,
    CoreImmutableStringRef modeName,
    CoreImmutableStringRef fromModeName
)
{
    CORE_DUMP_OBJ_TRACE(rl, __FUNCTION__);

    /* check run loop state...*/
    
/*    if (!Core_equal(modeName, fromModeName))
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
    } */
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

        mode = __CoreRunLoop_findMode(rl, modeName, true);
        if (mode != NULL)
        {
            // 1. If this loop is in sleep for the specified mode...
            if (__CoreRunLoop_isSleeping(rl) 
                && CoreSet_containsValue(rl->_currentModes, mode));
            {
                shouldWakeUp = true;
            }
            __CoreRunLoopMode_lock(mode);
            __CoreRunLoop_unlock(rl);
            if (CORE_UNLIKELY(mode->_timers == null))
            {
                mode->_timers = CoreSet_create(
                    Core_getAllocator(mode), 0, &CoreSetValueCoreCallbacks
                );
            }
            if (CORE_LIKELY(mode->_timers != null))
            {
                CoreSet_addValue(mode->_timers, rlt);
                
                // ... and 2. this timer is the minimal one, ...
                if (shouldWakeUp)
                {
                    CoreINT_S64 minFireTime;
                    
                    minFireTime = 0; //__CoreRunLoop_getMinFireTime(rl, mode); // TODO
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
        if ((mode->_timers != null) && (CoreSet_getCount(mode->_timers) > 0))
        {
            Core_retain(rlt); // for sure it doesn't free when removing...
            CoreSet_removeValue(mode->_timers, rlt);
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
        if (CORE_UNLIKELY(mode->_sources == null))
        {
            mode->_sources = CoreSet_create(
                Core_getAllocator(mode), 
                0, 
                &CoreSetValueCoreCallbacks
            );
        }
        if (CORE_LIKELY(mode->_sources != null))
        {
            CoreSet_addValue(mode->_sources, rls);
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
        if ((mode->_sources != null) 
            && (CoreSet_containsValue(mode->_sources, rls) > 0))
        {
            Core_retain(rls); // for sure it doesn't free when removing...
            CoreSet_removeValue(mode->_sources, rls);
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
        if (rlm->_sources != NULL)
        {
            result = CoreSet_containsValue(rlm->_sources, rls);
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
        if (mode->_observers == null)
        {
            mode->_observers = CoreSet_create(
                Core_getAllocator(mode), 0, &CoreSetValueCoreCallbacks
            );
        }
        if (mode->_observers != null)
        {
            CoreSet_addValue(mode->_observers, rlo);
            mode->_observerMask |= rlo->_activities;
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
        if ((mode->_observers != null) && (CoreSet_getCount(mode->_observers) > 0))
        {
            Core_retain(rlo); // for sure it doesn't free when removing...
           CoreSet_removeValue(mode->_observers, rlo);
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
    CoreArray_addValue(array, rlm->_name);
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

