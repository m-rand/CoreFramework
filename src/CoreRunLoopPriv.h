
#ifndef CoreRunLoopPriv_H 

#define CoreRunLoopPriv_H 


typedef enum CoreRunLoopSleepResult
{
    CORE_RUN_LOOP_SLEEP_TIMED_OUT = 1,
    CORE_RUN_LOOP_SLEEP_TIMEOUT_FIRED = 2,
    CORE_RUN_LOOP_SLEEP_WOKEN_UP = 3,
    CORE_RUN_LOOP_SLEEP_ERROR = 4
} CoreRunLoopSleepResult;


CORE_PROTECTED CoreRunLoopSourceRef
CoreRunLoopSource_createWithPriority(
    CoreAllocatorRef allocator,
    CoreRunLoopSourceDelegate * delegate,
    CoreRunLoopSourceUserInfo * userInfo,
    CoreINT_S32 priority);

#endif
