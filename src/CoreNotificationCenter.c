

#include "CoreNotificationCenter.h"
#include "CoreArray.h"
#include "CoreDictionary.h"
#include "CoreString.h"
#include "CoreSynchronisation.h"


#define CORE_NOTIFICATION_CENTER_USE_SPINLOCK 0


struct __CoreNotificationObserver
{
    CoreRuntimeObject core;
    const void * observer;      // the real observer
    CoreNotificationCallback callback;
    CoreObjectRef sender;       // sender of notification (can be null)
    CoreNotificationUserInfo userInfo;
    CoreINT_U32 options;        // for future use
};

typedef struct __CoreNotificationObserver * CoreNotificationObserverRef;

static CoreClassID CoreNotificationObserverID = CORE_CLASS_ID_UNKNOWN;



struct __CoreNotificationCenter
{
    CoreRuntimeObject core;
#ifdef CORE_NOTIFICATION_CENTER_USE_SPINLOCK    
    CoreSpinLock lock;
#else
    CoreReadWriteLock * lock;
#endif 
    CoreDictionaryRef registry; // keys: strings; values: CoreArray
};

static CoreClassID CoreNotificationCenterID = CORE_CLASS_ID_UNKNOWN;



static void
__CoreNotificationObserver_cleanup(CoreObjectRef o)
{
    CoreNotificationObserverRef _o = (CoreNotificationObserverRef) o;
    if ((_o->userInfo.release != null) && (_o->userInfo.info != null))
    {
        _o->userInfo.release(_o->userInfo.info);
    }
    _o->observer = null;
    _o->callback = null;
    _o->sender = null;
    _o->userInfo.info = null;
    _o->userInfo.retain = null;
    _o->userInfo.release = null;
    _o->options = 0;
}


static CoreBOOL
__CoreNotificationObserver_equal(CoreObjectRef a, CoreObjectRef b)
{
    CoreBOOL result = false;
    
    CoreNotificationObserverRef o1 = (CoreNotificationObserverRef) a;
    CoreNotificationObserverRef o2 = (CoreNotificationObserverRef) b;
    
    result = ((o1->observer == o2->observer) 
                && ((o1->sender == null) || (o1->sender == o2->sender)));
    
    return result;
}


static const CoreClass __CoreNotificationObserverClass =
{
    0x00,                            // version
    "CoreNotificationObserver",        // name
    NULL,                            // init
    NULL,                            // copy
    __CoreNotificationObserver_cleanup,             // cleanup
    __CoreNotificationObserver_equal,               // equal
    NULL,                // hash
    NULL // getCopyOfDescription
};



CORE_PUBLIC CoreNotificationObserverRef
CoreNotificationObserver_create(
    CoreAllocatorRef allocator,
    const void * observer,
    CoreNotificationCallback callback,
    CoreObjectRef sender,
    CoreINT_U32 options,
    CoreNotificationUserInfo * userInfo    
)
{
    CoreNotificationObserverRef result = null;
    CoreINT_U32 size = sizeof(struct __CoreNotificationObserver);
    
    result = (CoreNotificationObserverRef) CoreRuntime_createObject(
        allocator, CoreNotificationObserverID, size
    );
    if (result != null)
    {
        result->observer = observer;
        result->callback = callback;
        result->sender = sender;
        result->options = options;
        memset(&result->userInfo, 0, sizeof(result->userInfo));
        if (userInfo != null)
        {
            result->userInfo = *userInfo;
            if ((userInfo->info != null) && (userInfo->retain != null))
            {
                userInfo->retain(userInfo->info);
            }
        }
    }
    
    return result;    
}



static void
__CoreNotificationCenter_cleanup(CoreObjectRef center)
{
    CoreNotificationCenterRef c = (CoreNotificationCenterRef) center;

    // registry is currently always a valid core object, but for sure...
    if (c->registry != null)
    {
        Core_release(c->registry);
    }
}



static const CoreClass __CoreNotificationCenterClass =
{
    0x00,                            // version
    "CoreNotificationCenter",        // name
    NULL,                            // init
    NULL,                            // copy
    __CoreNotificationCenter_cleanup,             // cleanup
    NULL,                            // equal
    NULL,                            // hash
    NULL // getCopyOfDescription
};

/* CORE_PROTECTED */ void
CoreNotificationCenter_initialize(void)
{
    CoreNotificationCenterID = CoreRuntime_registerClass(&__CoreNotificationCenterClass);
    CoreNotificationObserverID = CoreRuntime_registerClass(&__CoreNotificationObserverClass);
}




static CoreSpinLock __CoreNotificationCenterLock = CORE_SPIN_LOCK_INIT;
static CoreNotificationCenterRef __center = null; // singleton


/* CORE_PUBLIC */ CoreNotificationCenterRef
CoreNotificationCenter_create(void)
{
    CoreSpinLock_lock(&__CoreNotificationCenterLock);
    if (__center == null)
    {
        CoreINT_U32 size = sizeof(struct __CoreNotificationCenter);
        
        __center = (CoreNotificationCenterRef) CoreRuntime_createObject(
            CORE_ALLOCATOR_SYSTEM, CoreNotificationCenterID, size
        );
        if (__center != null)
        {
#ifdef CORE_NOTIFICATION_CENTER_USE_SPINLOCK
            (void) CoreSpinLock_init(&__center->lock);
#else
            __center->lock = CoreReadWriteLock_create(CORE_ALLOCATOR_SYSTEM, 64);
#endif
            __center->registry = CoreDictionary_create(
                null, 
                0, 
                &CoreDictionaryKeyCoreCallbacks, 
                &CoreDictionaryValueCoreCallbacks
            );
#ifdef CORE_NOTIFICATION_CENTER_USE_SPINLOCK
            if (__center->registry == null)
            {
                CoreAllocator_deallocate(CORE_ALLOCATOR_SYSTEM, __center);
                __center = null;
            }
#else
            if ((__center->lock == null) || (__center->registry == null))
            {
                // roll back
                if (__center->lock != null) 
                {
                    CoreReadWriteLock_destroy(
                        CORE_ALLOCATOR_SYSTEM, __center->lock
                    );
                }
                if (__center->registry != null)
                {
                    Core_release(__center->registry);
                }
                CoreAllocator_deallocate(CORE_ALLOCATOR_SYSTEM, __center);
                __center = null;
            }
#endif
        }
    }
    CoreSpinLock_unlock(&__CoreNotificationCenterLock);
    
    return __center;    
}


/* CORE_PUBLIC */ CoreNotificationCenterRef
CoreNotificationCenter_getCenter(void)
{
    return CoreNotificationCenter_create();
}


/* CORE_PUBLIC */ void
CoreNotificationCenter_addObserver(
    CoreNotificationCenterRef center,
    const void * observer,
    CoreNotificationCallback callback,
    CoreImmutableStringRef name,
    const void * sender,
    CoreINT_U32 options,
    CoreNotificationUserInfo * userInfo
)
{
    CoreObjectRef * observers = null;

    if (name == null)
    {
        name = CORE_EMPTY_STRING;
    }
    
    // wlock
#ifdef CORE_NOTIFICATION_CENTER_USE_SPINLOCK    
    CoreSpinLock_lock(&center->lock);
#else
    CoreReadWriteLock_lockWrite(center->lock);
#endif
    
    observers = CoreDictionary_getValue(center->registry, name);
    if (observers == null)
    {
        // first observer of this notification
        CoreAllocatorRef allocator = Core_getAllocator(center);
        CoreNotificationObserverRef o = CoreNotificationObserver_create(
            allocator, observer, callback, sender, options, userInfo
        );
        CoreArrayRef array = CoreArray_create(
            allocator, 0, &CoreArrayCoreCallbacks
        );
    
        if ((array != null) && (o != null))
        {
            CoreArray_addValue(array, o);
            CoreDictionary_addValue(center->registry, name, array);
            Core_release(o);
            Core_release(array);
        }
    }
    else
    {
        CoreArrayRef array = (CoreArrayRef) observers;
        CoreAllocatorRef allocator = Core_getAllocator(center);
        CoreNotificationObserverRef o = CoreNotificationObserver_create(
            allocator, observer, callback, sender, options, userInfo
        );
        if (o != null)
        {
            CoreArray_addValue(array, o);
            Core_release(o);
        }
    }
    
    // wunlock
#ifdef CORE_NOTIFICATION_CENTER_USE_SPINLOCK    
    CoreSpinLock_unlock(&center->lock);
#else
    CoreReadWriteLock_unlockWrite(center->lock);
#endif
}




struct __CollectObserver
{
    CoreNotificationObserverRef observer;
    const void * object;
    CoreObjectRef * names;
};

static void
__CoreNotificationCenter_removeObserverApplier(
    const void * key,
    const void * value,
    void * context
)
{
    CoreImmutableStringRef name = (CoreImmutableStringRef) key;
    CoreArrayRef observers = (CoreArrayRef) value;
    struct __CollectObserver * collector = (struct __CollectObserver *) context;
    
    if (observers != null)
    {
        struct __CoreNotificationObserver tmp;
        CoreINT_U32 idx, n;
        
        CoreRuntime_initStaticObject(&tmp, CoreNotificationObserverID);
        tmp.observer = collector->observer;
        tmp.sender = collector->object;
        
        idx = CoreArray_getFirstIndexOfValue(
            observers, CoreRange_make(0, CoreArray_getCount(observers)), &tmp
        );
        if (idx != CORE_INDEX_NOT_FOUND)
        {
            CoreArray_removeValueAtIndex(observers, idx);
        }
        
        // Is there still any observer? If not, we will remove the notification
        // from registry later.
        n = CoreArray_getCount(observers);
        if (n == 0)
        {
            if (*(collector->names) == null)
            {
                // first notification name that is to be removed
                *(collector->names) = Core_retain(name);
            }
            else if (Core_getClassID(*(collector->names)) == CoreString_getClassID())
            {
                // the second one was found, need to create an array
                CoreArrayRef array = CoreArray_create(
                    CORE_ALLOCATOR_SYSTEM, 0, &CoreArrayCoreCallbacks);
                if (array != null)
                {
                    CoreArray_addValue(array, *(collector->names));
                    CoreArray_addValue(array, name);
                    Core_release(*(collector->names));
                    *(collector->names) = array;
                }
            }
            else // Core_getClassID(*(collector->names)) == CoreArray_getClassID()
            {
                // an array was already created, so just add another item to it
                CoreArray_addValue((CoreArrayRef) *(collector->names), name);
            }
        }
    }
}
        
/* CORE_PUBLIC */ void
CoreNotificationCenter_removeObserver(
    CoreNotificationCenterRef center,
    const void * observer,
    CoreImmutableStringRef name,
    const void * sender
)
{
    // wlock
#ifdef CORE_NOTIFICATION_CENTER_USE_SPINLOCK    
    CoreSpinLock_lock(&center->lock);
#else
    CoreReadWriteLock_lockWrite(center->lock);
#endif

    if (name == null)
    {
        // remove the observer for all its notifications
        CoreObjectRef names = null;
        struct __CollectObserver collector = { observer, sender, &names };
        
        CoreDictionary_applyFunction(
            center->registry, 
            __CoreNotificationCenter_removeObserverApplier,
            &collector
        );
        
        //
        // now check whether we should remove some notifications from registry
        //
        if (collector.names != null)
        {
            if (Core_getClassID(collector.names) == CoreArray_getClassID())
            {
                CoreArrayRef array = (CoreArrayRef) collector.names;
                CoreINT_U32 idx, n;
                
                n = CoreArray_getCount(array);
                for (idx = 0; idx < n; idx++)
                {
                    CoreImmutableStringRef name;
                    
                    name = (CoreImmutableStringRef) CoreArray_getValueAtIndex(
                        array, idx
                    );
                    CoreDictionary_removeValue(center->registry, name);                        
                }
            }
            else
            {
                CoreDictionary_removeValue(center->registry, collector.names);            
            }
            Core_release(collector.names);
        }
    }
    else
    {
        // remove the observer for the specified notification
        CoreArrayRef observers;
        
        observers = (CoreArrayRef) CoreDictionary_getValue(center->registry, name);
        if (observers != null)
        {
            struct __CoreNotificationObserver tmp;
            CoreINT_U32 idx, n;
            
            CoreRuntime_initStaticObject(&tmp, CoreNotificationObserverID);
            tmp.observer = observer;
            tmp.sender = sender;
            
            idx = CoreArray_getFirstIndexOfValue(
                observers, CoreRange_make(0, CoreArray_getCount(observers)), &tmp
            );
            if (idx != CORE_INDEX_NOT_FOUND)
            {
                CoreArray_removeValueAtIndex(observers, idx);
            }

            n = CoreArray_getCount(observers);
            if (n == 0)
            {
                CoreDictionary_removeValue(center->registry, name);
            }
        }
    }

    // wunlock
#ifdef CORE_NOTIFICATION_CENTER_USE_SPINLOCK    
    CoreSpinLock_unlock(&center->lock);
#else
    CoreReadWriteLock_unlockWrite(center->lock);
#endif    
}


/* CORE_PUBLIC */ void
CoreNotificationCenter_postNotification(
    CoreNotificationCenterRef center,
    CoreImmutableStringRef name,
    const void * sender,
    const void * data,
    CoreINT_U32 options
)
{
    CoreArrayRef observers;
    
    if (name == null)
    {
        name = CORE_EMPTY_STRING;
    }

    // rlock
#ifdef CORE_NOTIFICATION_CENTER_USE_SPINLOCK    
    CoreSpinLock_lock(&center->lock);
#else
    CoreReadWriteLock_lockRead(center->lock);
#endif

    observers = CoreDictionary_getValue(center->registry, name);
    if (observers != null)
    {
        CoreNotificationObserverRef buffer[32] = { null };
        CoreNotificationObserverRef * toObserve = buffer;
        CoreINT_U32 idx, n;
        
        n = CoreArray_getCount(observers);
        if (n > 32)
        {
            toObserve = CoreAllocator_allocate(
                Core_getAllocator(center), 
                n * sizeof(CoreNotificationObserverRef)
            );
        }         
        CoreArray_copyValues(
            observers, CoreRange_make(0, CoreArray_getCount(observers)), toObserve
        );
        
        for (idx = 0; idx < n; idx++)
        {
            (void) Core_retain(toObserve[idx]);
        }

        // runlock
#ifdef CORE_NOTIFICATION_CENTER_USE_SPINLOCK    
        CoreSpinLock_unlock(&center->lock);
#else
        CoreReadWriteLock_unlockRead(center->lock);
#endif

        // callout
        for (idx = 0; idx < n; idx++)
        {
            CoreNotificationObserverRef o = toObserve[idx];
            if ((o->sender == null) || (o->sender == sender))
            {
                o->callback(center, name, o->observer, sender, data, o->userInfo.info);
            }
            Core_release(toObserve[idx]);
        }
        
        if (toObserve != buffer)
        {
            CoreAllocator_deallocate(Core_getAllocator(center), toObserve);
        }
    }
    else
    {
        // runlock
#ifdef CORE_NOTIFICATION_CENTER_USE_SPINLOCK    
        CoreSpinLock_unlock(&center->lock);
#else
        CoreReadWriteLock_unlockRead(center->lock);
#endif
    }
}


