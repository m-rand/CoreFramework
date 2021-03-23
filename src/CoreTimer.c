


struct __CoreTimerManager
{
    CoreRuntimeObject base;
    CoreSetRef timers;
    
};

static __CoreTimerManager mgr;

void
CoreTimerManager_initialize(void)
{
    static volatile CoreBOOL init = false;
    
    if (!init)
    {
        init = true;
        CoreRuntime_initStaticObject(&manager);
    }
}


static socket selfpipe[2] = { INVALID_SOCKET, INVALID_SOCKET };

#if defined(__LINUX__) 
init()
{
    int ok;
    
    ok = socketpair(PF_LOCAL, SOCK_DGRAM, 0, selfpipe);
    if (ok == 0)
    {
        int oldFlag = fcntl(selfpipe[0], F_GETFL, 0);
        
        fcntl(selfpipe[0], F_SETFL, oldFlag | O_NONBLOCK);
        fcntl(selfpipe[1], F_SETFL, oldFlag | O_NONBLOCK);
    }
}
#elif defined(__WIN32__)

#endif


static void
__CoreTimerManager_addTimer(CoreTimerRef timer)
{
    CoreTimerRef minTimer = null;
    
    __CoreTimerManager_lock();
    if (CORE_UNLIKELY(mgr->timers == null))
    {
        mgr->timers = CorePriorityQueue_create(null, 0, ...);
    }
    if (CORE_LIKELY(mgr->timers != null))
    {
        CorePriorityQueue_addValue(mgr->timers, timer);
        minTimer = CorePriorityQueue_getMinValue(mgr->timers);
    }
    __CoreTimerManager_unlock();

    if (minTimer == timer)
    {
        // wake up timer manager...
        int res;
        char c = 'w';
        
        do
        {
            res = send(selfpipe[0], &c, sizeof(c), 0);
        }
        while (res <= 0);
    }

}

void
CoreTimerManager_scheduleTimer(CoreTimerRef timer)
{
    CoreINT_S64 fireTime = 0;
    struct timespec now;
    struct timespec norm_now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    set_normalized_timespec(&norm_now, now.tv_sec, now.tv_nsec);
    fireTime = norm_now.tv_sec * 1000;
    fireTime += norm_now.tv_nsec / 1000000;
    _CoreTimer_setFireTime(timer, fireTime);
    
    __CoreTimerManager_addTimer(timer);
    _CoreTimer_setScheduled(timer, true);
}


static CoreComparison
CoreTimerCompare(const void * a, const void * b)
{
    CoreComparison result;
    
    const CoreTimerRef * _a = (const CoreTimerRef *) a;
    const CoreTimerRef * _b = (const CoreTimerRef *) b;
    if (_a->fireTime < _b->fireTime)
    {
        result = CORE_COMPARISON_LESS_THAN;
    }
    else if (_a->fireTime > _b->fireTime)
    {
        result = CORE_COMPARISON_GREATER_THAN;
    }
    else
    {
        result = CORE_COMPARISON_EQUAL;
    }
    
    return result;
}

static void 
__CoreTimerManager_findMinTimer(const void * value, void * context)
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
__CoreTimerManager_getMinFireTime(CoreRunLoopRef me, CoreRunLoopModeRef mode)
{
    CoreINT_U32 result = 0;
    
    if ((mgr->timers != null) && (CoreSet_getCount(mgr->timers) > 0)
    {
        CoreTimerRef minTimer = null;
        
        CoreSet_applyFunction(
            mgr->timers, 
            __CoreTimerManager_findMinTimer,
            &minTimer
        );
        if (minTimer != null)
        {
            result = minTimer->fireTime;
        }        
    }
    
    return result;
}

static void
loop(void)
{
    
    struct pollfd fds[1];
    CoreINT_S64 sleepTime;
    struct timespec now;
    
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    do
    {
        int res;
        CoreINT_S64 fireTime;
        
        fireTime = __CoreTimerManager_getMinFireTime();
        fds[0].fd = selfpipe[1];
        fds[0].events = POLLIN;
        

        //
        // Compute sleep time...
        //
        if (fireTime >= 0)
        {
            struct timespec norm_now;
            CoreINT_S64 now_ns;
            CoreINT_S64 now_ms;
            
            set_normalized_timespec(&norm_now, now.tv_sec, now.tv_nsec);
            now_ns = timespec_to_ns(timespec_now);
            now_ms = now_ns * 1000000;
            sleepTime = fireTime - now_ms;
        }
        else
        {
            sleepTime = -1;
        }
        

        //
        // Sleep...
        //
        do
        {
            // check it properly!
            CoreINT_S32 sleepTime_poll = (CoreINT_S32) sleepTime;
            
            res = poll(fds, 1, sleepTime_poll);
        }
        while (result == -EINTR);
        
        
        //
        // Evaluation...
        //  == 0: timeout expired
        //   > 0: externally woken up
        //   < 0: error (??)
        //
        if (result == 0)
        {
            CoreTimerRef timer;
            CoreINT_U64 sleepTime_ns;
            
            timer = CorePriorityQueue_removeTop(mgr->timers);
                        
            // Signal the timer and wake up its run loop.
            CoreTimer_signal(timer);
            CoreRunLoop_wakeUp(timer->runLoop);
            
            // Reschedule if period is set
            sleepTime_ns = sleepTime * 1000000;
            timespec_add_ns(now, sleepTime_ns); // set for next loop
            if (timer->period > 0)
            {
                timer->fireTime = timespec_to_ms(now) + period;
                __CoreTimerManager_addTimer(timer); 
            }
        }
        else
        {
            struct timespec after, delta;
            
            if ((result > 0) && (fds[0].revents & POLLIN))
            {
                char buff[124] = {0};   // actually, there might be more than
                                        // 1 char in the pipe
                int num = recv(selfpipe[1], buff, 124, 0);
            }            

            clock_gettime(CLOCK_MONOTONIC, &after);
            delta = timespec_sub(after, now);
            now = delta; // set for next loop
        }
    } 
    while (1);

}

