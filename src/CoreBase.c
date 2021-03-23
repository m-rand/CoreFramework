
#include <CoreFramework/CoreBase.h>
#include "CoreRuntime.h"
#include "CoreInternal.h"
#include "CoreString.h"




/*****************************************************************************
 *
 *  LOGGING MECHANISMS PUBLIC API
 *  
 *****************************************************************************/
    
static FILE * _logFile = null;
static CoreINT_U32 _logLevel = 0;

/* CORE_PROTECTED */ FILE * 
Core_getLogFile(void)
{
    return _logFile;
}

/* CORE_PROTECTED */ CoreINT_U32 
Core_getLogLevel(void)
{
    return _logLevel;
}

/* CORE_PROTECTED */ void
Core_setLogInfo(FILE * logFile, CoreINT_U32 logLevel)
{
    if (logFile != null)
    {
        _logFile = logFile;
        _logLevel = logLevel;
    }
}




/*****************************************************************************
 *
 *  CoreRange
 *  
 *****************************************************************************/

/* CORE_PUBLIC */ CoreRange 
__CoreRange_create(CoreINT_U32 offset, CoreINT_U32 length)
{
    CoreRange range = { offset, length };
    return range;    
}

/* CORE_PUBLIC */ CoreRange 
__CoreRange_make(CoreINT_U32 offset, CoreINT_U32 length)
{
    CoreRange range = { offset, length };
    return range;    
}



/*****************************************************************************
 *
 *  CoreAllocator
 *  
 *****************************************************************************/

struct __CoreAllocator
{
    CoreRuntimeObject core;
    CoreAllocatorDelegate delegate;
};


static CoreClassID CoreAllocatorID = CORE_CLASS_ID_UNKNOWN;

static CoreAllocatorRef theCurrent = NULL;



static void
__CoreAllocator_cleanup(CoreObjectRef me)
{
    CoreAllocatorRef _me = (CoreAllocatorRef) me;
    
    
}


static CoreImmutableStringRef
__CoreAllocator_getCopyOfDescription(CoreObjectRef me)
{
    CoreImmutableStringRef result = null;
    CoreAllocatorRef _me = (CoreAllocatorRef) me;
    char s[100];
    
    sprintf(s, "CoreAllocator <%p>{info = %p}\n", me, _me->delegate.info);
    result = CoreString_createImmutableWithASCII(null, s, strlen(s));

    return result;
}


/* synchronized */
/* CORE_PUBLIC */ CoreAllocatorRef
CoreAllocator_getDefault(void)
{
    return theCurrent;
}

/* synchronized */
/* CORE_PUBLIC */ void
CoreAllocator_setDefault(CoreAllocatorRef allocator)
{
    if (allocator == null) 
    {
        allocator = CoreAllocator_getDefault();
    }
    
    if (allocator != theCurrent)
    {
        if (theCurrent != null)
        {
            Core_release(theCurrent);
        }
        theCurrent = Core_retain(allocator);
        /*
        CoreBOOL success = false;
        
        do
        {
            volatile CoreAllocatorRef old = theCurrent;
            
            success = CORE_ATOMIC_COMPARE_AND_SWAP_PTR(
                (void * volatile *) &theCurrent,
                (const void *) old,
                allocator
            );
        }
        while (!success);
        
        if (old != NULL)
        {
            Core_release(old);
        }
        
        Core_retain(allocator);
        */
    }
}




/* CORE_PUBLIC */ void
CoreAllocator_copyAllocatorDelegate(
    CoreAllocatorRef me, 
    CoreAllocatorDelegate * delegate
)
{
    if (me == null) 
    {
        me = CoreAllocator_getDefault();
    }
    
    if (delegate != NULL)
    {
        *delegate = me->delegate;
    }                
}


/* CORE_PUBLIC */ CoreAllocatorRef
CoreAllocator_create(
    CoreAllocatorRef allocator, 
    CoreAllocatorDelegate * delegate,
    CoreBOOL useDelegate
)
{
    struct __CoreAllocator * result = null;
    
    if (delegate != null)
    {
        result->delegate = *delegate;
        if (delegate->retainInfo != null)
        {
            delegate->retainInfo(result->delegate.info);
        }
        
        if (useDelegate)
        {
            result = delegate->allocate(
                sizeof(struct __CoreAllocator),
                result->delegate.info
            );
        }
        else
        {
            if (allocator == null)
            {
                allocator = CoreAllocator_getDefault();
                result = CoreAllocator_allocate(
                    allocator, 
                    sizeof(struct __CoreAllocator)
                );
            }
        }
        
        if (result != null)
        {
            _Core_setRetainCount(result, 1);
            _Core_setObjectClassID(result, CoreAllocatorID);
            ((CoreRuntimeObject *)result)->isa = 
                _Core_getISAForClassID(CoreAllocatorID);
        }
    }
    
    return result;
}


static void *
__CoreAllocatorEmpty_allocate(CoreINT_U32 size, const void * info)
{
    return null;
}

static void *
__CoreAllocatorEmpty_reallocate(
    void * memPtr, CoreINT_U32 newSize, const void * info
)
{
    return null;
}

static void
__CoreAllocatorEmpty_deallocate(void * memPtr, const void * info)
{
    return ;
}


static void *
__CoreAllocatorSystem_allocate(CoreINT_U32 size, const void * info)
{
    return malloc(size);
}

static void *
__CoreAllocatorSystem_reallocate(
    void * memPtr, CoreINT_U32 newSize, const void * info
)
{
    return realloc(memPtr, newSize);
}

static void
__CoreAllocatorSystem_deallocate(void * memPtr, const void * info)
{
    free(memPtr);
}


/* CORE_PUBLIC */ void *
CoreAllocator_allocate(CoreAllocatorRef me, CoreINT_U32 size)
{
    void * result = null;
    
    if (CORE_LIKELY(me->delegate.allocate != null))
    {
        result = me->delegate.allocate(size, me->delegate.info);
    }
    else
    {
        result = CoreAllocator_reallocate(me, null, size);
    }
    
    return result;
}


/* CORE_PUBLIC */ void *
CoreAllocator_reallocate(CoreAllocatorRef me, void * memPtr, CoreINT_U32 newSize)
{
    void * result = null;
    
    if (CORE_LIKELY(me->delegate.reallocate != null))
    {
        result = me->delegate.reallocate(memPtr, newSize, me->delegate.info);
    }
    else
    {
        /*
         * Now we simmulate reallocation on the following basis:
         *          
         *  memPtr      |   newSize     |   result
         *  ----------------------------------------         
         *  null        |   0           |   nothing
         *  null        |   > 0         |   allocate
         *  non-null    |   0           |   deallocate
         *  non-null    |   > 0         |   deallocate old + allocate new
         */ 
                                                             
        if (newSize == 0)
        {
            if (memPtr != null)
            {
                CoreAllocator_deallocate(me, memPtr);
            }
        }
        else
        {
            if (memPtr == null)
            {
                result = CoreAllocator_allocate(me, newSize);
            }
            else
            {
                CoreAllocator_deallocate(me, memPtr);
                memPtr = CoreAllocator_allocate(me, newSize);
                result = memPtr;
            }
        }
    }
    
    return result;
}


/* CORE_PUBLIC */ void
CoreAllocator_deallocate(CoreAllocatorRef me, void * memPtr)
{
    if (CORE_LIKELY(me->delegate.deallocate != null))
    {
        me->delegate.deallocate(memPtr, me->delegate.info);
    }
    else
    {
        (void) CoreAllocator_reallocate(me, memPtr, 0);
    }
}


static const CoreClass __CoreAllocatorClass =
{
    0x00,                           // version
    "CoreAllocator",                // name
    NULL,                           // init
    NULL,                           // copy
    __CoreAllocator_cleanup,        // cleanup
    NULL,                           // equal
    NULL,                           // hash
    __CoreAllocator_getCopyOfDescription // getCopyOfDescription
};

static struct CoreAllocatorDelegate __CoreAllocatorSystemDelegate = 
{
        NULL, 
        NULL, 
        NULL, 
        NULL, 
        __CoreAllocatorSystem_allocate, 
        __CoreAllocatorSystem_reallocate, 
        __CoreAllocatorSystem_deallocate 
};

static struct __CoreAllocator __CoreAllocatorSystem =
{
    CORE_INIT_RUNTIME_CLASS(),
    {
        NULL, 
        NULL, 
        NULL, 
        NULL, 
        __CoreAllocatorSystem_allocate, 
        __CoreAllocatorSystem_reallocate, 
        __CoreAllocatorSystem_deallocate 
    }
};

const CoreAllocatorRef CORE_ALLOCATOR_SYSTEM = &__CoreAllocatorSystem;



static struct __CoreAllocator __CoreAllocatorEmpty =
{
    CORE_INIT_RUNTIME_CLASS(),
    {
        NULL, 
        NULL, 
        NULL, 
        NULL, 
        __CoreAllocatorEmpty_allocate, 
        __CoreAllocatorEmpty_reallocate, 
        __CoreAllocatorEmpty_deallocate, 
    }
};

const CoreAllocatorRef CORE_ALLOCATOR_EMPTY = &__CoreAllocatorEmpty;


/* CORE_PROTECTED */ void
CoreAllocator_initialize(void)
{
    CoreAllocatorID = CoreRuntime_registerClass(&__CoreAllocatorClass);
    CoreRuntime_initStaticObject(CORE_ALLOCATOR_SYSTEM, CoreAllocatorID);
    //__CoreAllocatorSystem.delegate = __CoreAllocatorSystemDelegate;
    CoreAllocator_setDefault(CORE_ALLOCATOR_SYSTEM);
    //theCurrent = CORE_ALLOCATOR_SYSTEM; 
    CoreRuntime_initStaticObject(CORE_ALLOCATOR_EMPTY, CoreAllocatorID);       
}




/* CORE_PROTECTED */ void
CoreBase_initialize(void)
{
    _logFile = stdout;
}

