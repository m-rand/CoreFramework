
/*********************************************************************
	Author			: Marcel Balas
	Name			: CoreRunLoop
	Generated Date	: 2009-09-09  
*********************************************************************/

/*****************************************************************************
*
*
*
*****************************************************************************/

#ifndef CoreRunLoop_H 

#define CoreRunLoop_H 


#include "CoreBase.h"
#include "CoreArray.h"


typedef struct __CoreRunLoop * CoreRunLoopRef;

typedef struct __CoreRunLoopMode * CoreRunLoopModeRef;

typedef struct __CoreTimer * CoreTimerRef;

typedef struct __CoreRunLoopSource * CoreRunLoopSourceRef;

typedef struct __CoreRunLoopObserver * CoreRunLoopObserverRef;


typedef enum CoreRunLoopResult
{
    CORE_RUN_LOOP_SOURCE_HANDLED    = 1,
    CORE_RUN_LOOP_TIMED_OUT         = 2,
    CORE_RUN_LOOP_STOPPED           = 3,
    CORE_RUN_LOOP_FINISHED          = 4,
} CoreRunLoopResult;

typedef enum CoreRunLoopActivity
{
    CORE_RUN_LOOP_ENTRY             = (1 << 0),
    CORE_RUN_LOOP_CHECK_TIMERS      = (1 << 1),
    CORE_RUN_LOOP_CHECK_SOURCES     = (1 << 2),
    CORE_RUN_LOOP_SLEEP             = (1 << 3),
    CORE_RUN_LOOP_WAKE_UP           = (1 << 4),
    CORE_RUN_LOOP_EXIT              = (1 << 5),
    CORE_RUN_LOOP_ALL_ACTIVITIES    = (
        CORE_RUN_LOOP_ENTRY | CORE_RUN_LOOP_CHECK_TIMERS |
        CORE_RUN_LOOP_CHECK_SOURCES | CORE_RUN_LOOP_SLEEP |
        CORE_RUN_LOOP_WAKE_UP | CORE_RUN_LOOP_EXIT)    
} CoreRunLoopActivity;


/*typedef struct CoreRunLoopSourceDelegate
{
    void * info;
    const void * (* retain) (const void * info);
    void (* release) (const void * info);
    CoreImmutableStringRef (* getCopyOfDescription)(const void * info);
    CoreBOOL (* equal) (const void * info1, const void * info2);
    CoreHashCode (* hash) (const void * info);
    
    void (* schedule) (
        void * info, CoreRunLoopRef runLoop, CoreImmutableStringRef mode
    );
    void (* cancel) (
        void * info, CoreRunLoopRef runLoop, CoreImmutableStringRef mode
    ); 
    void (* perform) (void * info);
} CoreRunLoopSourceDelegate;*/


typedef struct CoreRunLoopSourceUserInfo
{
    void * info;
    const void * (* retain) (const void * info);
    void (* release) (const void * info);
    CoreImmutableStringRef (* getCopyOfDescription)(const void * info);
    CoreBOOL (* equal) (const void * info1, const void * info2);
    CoreHashCode (* hash) (const void * info);
} CoreRunLoopSourceUserInfo;

typedef struct CoreRunLoopSourceDelegate
{
    void (* schedule) (
        void * info, CoreRunLoopRef runLoop, CoreImmutableStringRef mode
    );
    void (* cancel) (
        void * info, CoreRunLoopRef runLoop, CoreImmutableStringRef mode
    ); 
    void (* perform) (void * info);
} CoreRunLoopSourceDelegate;



typedef struct CoreTimerUserInfo
{
    void * info;
    const void * (* retain) (const void * info);
    void (* release) (const void * info);
    CoreImmutableStringRef (* getCopyOfDescription)(const void * info);
} CoreTimerUserInfo;

typedef void (* CoreTimerCallback) (CoreTimerRef timer, void * info);


typedef struct CoreRunLoopObserverUserInfo
{
    void * info;
    const void * (* retain) (const void * info);
    void (* release) (const void * info);
    CoreImmutableStringRef (* getCopyOfDescription)(const void * info);
} CoreRunLoopObserverUserInfo;

typedef void (* CoreRunLoopObserverCallback) (
    CoreRunLoopObserverRef observer,
    CoreRunLoopActivity activity,
    void * info
);





CORE_PUBLIC CoreImmutableStringRef CORE_RUN_LOOP_MODE_DEFAULT;



CORE_PUBLIC void 
CoreRunLoop_run(void);

CORE_PUBLIC CoreRunLoopResult
CoreRunLoop_runInMode(
    CoreImmutableStringRef mode, 
    CoreINT_S64 delay,
    CoreBOOL returnAfterHandle); 

CORE_PUBLIC void 
CoreRunLoop_wakeUp(CoreRunLoopRef me);

CORE_PUBLIC void 
CoreRunLoop_stop(CoreRunLoopRef me);

CORE_PUBLIC CoreRunLoopRef
CoreRunLoop_getCurrent(void);

CORE_PUBLIC CoreImmutableStringRef 
CoreRunLoop_getCurrentModeName(CoreRunLoopRef rl);

CORE_PUBLIC void 
CoreRunLoop_addMode(
    CoreRunLoopRef rl,
    CoreImmutableStringRef modeName,
    CoreImmutableStringRef toModeName);

CORE_PUBLIC void 
CoreRunLoop_removeSubmodeFromMode(
    CoreRunLoopRef rl,
    CoreImmutableStringRef modeName,
    CoreImmutableStringRef fromModeName);

CORE_PUBLIC void 
CoreRunLoop_addTimer(
    CoreRunLoopRef me,
    CoreTimerRef timer,
    CoreImmutableStringRef modeName);

CORE_PUBLIC void 
CoreRunLoop_removeTimer(
    CoreRunLoopRef me,
    CoreTimerRef timer,
    CoreImmutableStringRef modeName);
        
CORE_PUBLIC void 
CoreRunLoop_addSource(
    CoreRunLoopRef me,
    CoreRunLoopSourceRef source,
    CoreImmutableStringRef modeName);

CORE_PUBLIC void 
CoreRunLoop_removeSource(
    CoreRunLoopRef me,
    CoreRunLoopSourceRef source,
    CoreImmutableStringRef modeName);

CORE_PUBLIC CoreBOOL
CoreRunLoop_containsSource(    
    CoreRunLoopRef rl,
    CoreRunLoopSourceRef rls,
    CoreImmutableStringRef mode);
    
CORE_PUBLIC void 
CoreRunLoop_addObserver(
    CoreRunLoopRef rl,
    CoreRunLoopObserverRef rlo,
    CoreImmutableStringRef modeName);

CORE_PUBLIC void 
CoreRunLoop_removeObserver(
    CoreRunLoopRef rl,
    CoreRunLoopObserverRef rlo,
    CoreImmutableStringRef modeName);

CORE_PUBLIC CoreImmutableArrayRef
CoreRunLoop_getCopyOfModes(CoreRunLoopRef rl);

CORE_PUBLIC void 
CoreRunLoopSource_signal(CoreRunLoopSourceRef rls);

CORE_PUBLIC void
CoreRunLoopSource_cancel(CoreRunLoopSourceRef source);

CORE_PUBLIC CoreRunLoopRef 
CoreRunLoopSource_getRunLoop(CoreRunLoopSourceRef rls);

CORE_PUBLIC void
CoreTimer_cancel(CoreTimerRef timer);



CORE_PUBLIC CoreRunLoopRef 
CoreRunLoop_create(void);

CORE_PUBLIC CoreRunLoopObserverRef 
CoreRunLoopObserver_create(
    CoreAllocatorRef allocator,
    CoreRunLoopActivity activities,
    CoreRunLoopObserverCallback callback,
    CoreRunLoopObserverUserInfo * userInfo);


CORE_PUBLIC CoreRunLoopSourceRef
CoreRunLoopSource_create(
    CoreAllocatorRef allocator,
    CoreRunLoopSourceDelegate * delegate,
    CoreRunLoopSourceUserInfo * userInfo);

CORE_PUBLIC CoreTimerRef 
CoreTimer_create(
    CoreAllocatorRef allocator,
    CoreINT_U32 delay,
    CoreINT_U32 period,
    CoreTimerCallback callback,
    CoreTimerUserInfo * userInfo);
    


CORE_PROTECTED void 
CoreRunLoop_initialize(void);


#endif
