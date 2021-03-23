

#include "CoreRuntime.h"
#include "CoreData.h"
#include "CoreString.h"
#include "CoreArray.h"
#include "CoreDictionary.h"
#include "CoreSet.h"
#include "CoreRunLoop.h"
#include "CoreNotificationCenter.h"
#include "CoreMessagePort.h"
#include "CoreInternal.h"
#include "CoreSynchronisation.h"




/*
 * info = 0000 0000 0000 0000 0000 0000 0000 0000
 *        AAAA AAAA AAAA AAAA BBBB BBBB CCDD DDDD
 * where:
 *  A - retain count
 *  B - classID
 *  C - allocator
 *  D - private object's info 
 */   

#define CORE_OBJECT_RC_START            16
#define CORE_OBJECT_RC_LENGTH           16  /* ended at 31st bit */

#define CORE_OBJECT_CLASS_ID_START      8
#define CORE_OBJECT_CLASS_ID_LENGTH     8  /* ended at 15th bit */

#define CORE_OBJECT_ALLOCATOR_START     6  
#define CORE_OBJECT_ALLOCATOR_LENGTH    2  /* ended at 7th bit */

/*
 * Custom object's information at bits 0 .. 5. (6 bits)  
 */


#define CORE_OBJECT_SYSTEM_ALLOCATOR    0
#define CORE_OBJECT_CUSTOM_ALLOCATOR    1



#define __CORE_VALIDATE_OBJECT_RET0(o) 
#define __CORE_VALIDATE_OBJECT_RET1(o, ret)

/* 
#define __CORE_VALIDATE_OBJECT_RET0(o) \
    CORE_ASSERT_RET0( \
        (o != null) && (__Core_getObjectClassID(o) && \
        (__Core_getObjectClassID(o) != CoreUnknownID) && \
        (__Core_getObjectClassID(o) != CoreGenericID), \
        CORE_LOG_ASSERT, \
        "%s(): pointer %p is not a Core object", __PRETTY_FUNCTION__, o \
    );

#define __CORE_VALIDATE_OBJECT_RET1(o, ret) \
    CORE_ASSERT_RET1( \
        ret, \
        (o != null) && (__Core_getObjectClassID(o) && \
        (__Core_getObjectClassID(o) != CoreUnknownID) && \
        (__Core_getObjectClassID(o) != CoreGenericID), \
        CORE_LOG_ASSERT, \
        "%s(): pointer %p is not a Core object", __PRETTY_FUNCTION__, o \
    );
*/

/* CORE_PROTECTED */ void
_Core_setRetainCount(CoreObjectRef o, CoreINT_U32 count)
{
    CoreRuntimeObject * _o = (CoreRuntimeObject *) o;
    CoreBitfield_setValue(
        _o->info,
        CORE_OBJECT_RC_START,
        CORE_OBJECT_RC_LENGTH,
        count
    );    
}


/* CORE_PUBLIC */ CoreINT_U32
Core_getRetainCount(CoreObjectRef me)
{
    CoreINT_U32 result = 0;
    CoreRuntimeObject * _me = (CoreRuntimeObject *) me;
        
    __CORE_VALIDATE_OBJECT_RET1(me, 0);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    
    result = CoreBitfield_getValue(
        _me->info,
        CORE_OBJECT_RC_START,
        CORE_OBJECT_RC_LENGTH
    );
    if (CORE_UNLIKELY(result == 0))
    {
        // static object
        result = CoreINT_U32_MAX;
    }
    
    return result;
}

   
/* CORE_PUBLIC */ CoreObjectRef
Core_retain(CoreObjectRef o)
{
    CoreRuntimeObject * _o = (CoreRuntimeObject *) o;
    volatile CoreINT_U32 * info = null;
    CoreINT_U32 refCount = 0;
   
    __CORE_VALIDATE_OBJECT_RET1(o, null);
    CORE_DUMP_OBJ_TRACE(o, __FUNCTION__);
    
    info = &_o->info;
    refCount = CoreBitfield_getValue(
        _o->info,
        CORE_OBJECT_RC_START,
        CORE_OBJECT_RC_LENGTH
    );
    if (CORE_LIKELY(refCount > 0))
    {
        CoreBOOL success = false;

        do
        {
            CoreINT_U32 initInfo = *info;
            CoreINT_U32 newInfo = initInfo; // due to volatile info it is OK
            
            newInfo += (1 << CORE_OBJECT_RC_START); // increment refCount
            success = __CoreAtomic_compareAndSwap32_barrier(
                (CoreINT_S32 *) info,
                *((CoreINT_S32 *) &initInfo),
                *((CoreINT_S32 *) &newInfo)
            );
            /*success = CORE_ATOMIC_COMPARE_AND_SWAP_32(
                (CoreINT_U32 *) info,
                *((CoreINT_U32 *) &newInfo),
                *((CoreINT_U32 *) &initInfo)
            );*/    
        }        
        while (!success);
    }
    
    return o;
}


/* CORE_PUBLIC */ void
Core_release2(CoreObjectRef o)
{
    CoreINT_U32 refCount = 0;
    CoreRuntimeObject * _o = (CoreRuntimeObject *) o;
    
    __CORE_VALIDATE_OBJECT_RET0(o);
    CORE_DUMP_OBJ_TRACE(o, __FUNCTION__);

    refCount = CoreBitfield_getValue(
        _o->info,
        CORE_OBJECT_RC_START,
        CORE_OBJECT_RC_LENGTH
    );
    if (CORE_LIKELY(refCount > 1))
    {
        volatile CoreINT_U32 * info = &_o->info;
        CoreBOOL success = false;

        do
        {
            CoreINT_U32 initInfo = *info;
            CoreINT_U32 newInfo = initInfo; // due to volatile info it is OK
            
            CoreBitfield_setValue(
                newInfo,
                CORE_OBJECT_RC_START,
                CORE_OBJECT_RC_LENGTH,
                refCount - 1                
            ); // decrement
            success = __CoreAtomic_compareAndSwap32_barrier(
                (CoreINT_S32 *) info,
                *((CoreINT_S32 *) &initInfo),
                *((CoreINT_S32 *) &newInfo)
            );    
            /*success = CORE_ATOMIC_COMPARE_AND_SWAP_32(
                (CoreINT_U32 *) info,
                *((CoreINT_U32 *) &newInfo),
                *((CoreINT_U32 *) &initInfo)
            );*/    
        }        
        while (!success);
    }  
    else if (refCount == 1)
    {
        CoreAllocatorRef allocator = CoreAllocator_getDefault();
        void (* cleanup)(CoreObjectRef);
        CoreINT_U32 allocatorType = 0;
        CoreINT_U8 * ref = (CoreINT_U8 *) o;
        
        allocatorType = CoreBitfield_getValue(
            _o->info, 
            CORE_OBJECT_ALLOCATOR_START, 
            CORE_OBJECT_ALLOCATOR_LENGTH
        );
        cleanup = ((CoreClass *)_o->isa)->cleanup;
        if (cleanup != null)
        {
            cleanup(o);
        }
        
        // Someone could retain me in between...
        // check refCount again!? ... and then what?!
        
        if (allocatorType != CORE_OBJECT_SYSTEM_ALLOCATOR)
        {
            allocator = Core_getAllocator(o);
            ref = ((CoreINT_U8 *) o - sizeof(CoreAllocatorRef));
        }
        CoreAllocator_deallocate(allocator, ref);
        
        if (allocatorType != CORE_OBJECT_SYSTEM_ALLOCATOR)
        {
            Core_release(allocator);
        }
    }
    else 
    {
        // refCount = 0 ... static object ... do nothing    
    }  
}


/* CORE_PUBLIC */ void
Core_release(CoreObjectRef o)
{
    CoreINT_U32 refCount = 0;
    CoreRuntimeObject * _o = (CoreRuntimeObject *) o;
    volatile CoreINT_U32 * info;
    CoreBOOL success = false;
    
    __CORE_VALIDATE_OBJECT_RET0(o);
    CORE_DUMP_OBJ_TRACE(o, __FUNCTION__);

    do
    {
        info = &_o->info;
        refCount = CoreBitfield_getValue(
            *info,
            CORE_OBJECT_RC_START,
            CORE_OBJECT_RC_LENGTH
        );
        if (CORE_UNLIKELY(refCount == 0))
        {
            break;
        }
        else if (CORE_UNLIKELY(refCount == 1))        
        {
            void (* cleanup)(CoreObjectRef);
            
            // Deallocation may happen...
            cleanup = ((CoreClass *)_o->isa)->cleanup;
            if (cleanup != null)
            {
                cleanup(o);
            }
            
            // Someone could retain me in between...
            // ... deallocate only if it hasn't happen.
            refCount = CoreBitfield_getValue(
                *info,
                CORE_OBJECT_RC_START,
                CORE_OBJECT_RC_LENGTH
            );
            success = (refCount == 1);
            if (CORE_LIKELY(success))
            {
                CoreAllocatorRef allocator = null;
                CoreINT_U32 allocatorType = 0;
                CoreINT_U8 * ref = (CoreINT_U8 *) o;
                
                allocatorType = CoreBitfield_getValue(
                    _o->info, 
                    CORE_OBJECT_ALLOCATOR_START, 
                    CORE_OBJECT_ALLOCATOR_LENGTH
                );
                if (allocatorType != CORE_OBJECT_SYSTEM_ALLOCATOR)
                {
                    allocator = Core_getAllocator(o);
                    ref = ((CoreINT_U8 *) o - sizeof(CoreAllocatorRef));
                }
                else
                {
                    allocator = CoreAllocator_getDefault();
                }
                CoreAllocator_deallocate(allocator, ref);
                
                if (allocatorType != CORE_OBJECT_SYSTEM_ALLOCATOR)
                {
                    Core_release(allocator);
                }
            }
        }
        else 
        {
            // refCount > 1 ... just decrement it
            CoreINT_U32 initInfo = *info;
            CoreINT_U32 newInfo = initInfo; // due to volatile info it is OK
            
            CoreBitfield_setValue(
                newInfo,
                CORE_OBJECT_RC_START,
                CORE_OBJECT_RC_LENGTH,
                refCount - 1                
            ); // decrement
            success = __CoreAtomic_compareAndSwap32_barrier(
                (CoreINT_U32 *) info,
                *((CoreINT_U32 *) &initInfo),
                *((CoreINT_U32 *) &newInfo)
            );    
            /*success = CORE_ATOMIC_COMPARE_AND_SWAP_32(
                (CoreINT_U32 *) info,
                *((CoreINT_U32 *) &newInfo),
                *((CoreINT_U32 *) &initInfo)
            );*/    
        }
    }
    while (CORE_UNLIKELY(!success));
}


/*
 * Intentionally not thread-safe!!!
 * Accessing the values in table is currently not protected. 
 */  
static const CoreClass ** CoreRuntimeClassTable = NULL;
CoreINT_U32 CoreRuntimeClassTableSize = 0;
static CoreINT_U32 CoreRuntimeClassTableCount = 0;

/* CORE_PROTECTED */ CoreClassID
CoreRuntime_registerClass(const CoreClass * cls)
{
    CoreClassID result = 0;
    CoreBOOL success = true;
    
    if (CoreRuntimeClassTableSize <= CoreRuntimeClassTableCount)
    {
        CoreINT_U32 oldSize = CoreRuntimeClassTableSize;
        CoreINT_U32 newSize;
        void * newTable;
        
        newSize = (oldSize == 0) ? 64 : (oldSize * 4);
        newTable = malloc(newSize * sizeof(CoreClass *));
        if (newTable != NULL)
        {
            if (oldSize > 0)
            {
                memcpy(
                    newTable, 
                    CoreRuntimeClassTable, 
                    newSize * sizeof(CoreClass *)
                );
            }
            memset(
                newTable, 
                0, 
                (newSize - oldSize) * sizeof(CoreClass *)
            );
            CoreRuntimeClassTable = (const CoreClass **) newTable;
            CoreRuntimeClassTableSize = newSize;
        }
        else
        {
            success = false;
        }
    }
    
    if (success)
    {
        CoreRuntimeClassTable[CoreRuntimeClassTableCount++] = cls;
        result = CoreRuntimeClassTableCount - 1;    
    }
    
    return result;
}


static CoreClassID CoreUnknownID = CORE_CLASS_ID_UNKNOWN;
static const CoreClass __CoreUnknownClass = 
{
    0x00,           
    "Unknown",      
    NULL,           
    NULL,           
    NULL,           
    NULL,           
    NULL,           
    NULL            
};

static CoreClassID CoreGenericID = CORE_CLASS_ID_UNKNOWN;
static const CoreClass __CoreGenericClass = 
{
    0x00,           
    "Generic",      
    NULL,           
    NULL,           
    NULL,           
    NULL,           
    NULL,           
    NULL            
};



CORE_INLINE CoreClassID
__Core_getObjectClassID(const void * o)
{
    return (CoreClassID) CoreBitfield_getValue(
        ((CoreRuntimeObject *) o)->info,
        CORE_OBJECT_CLASS_ID_START,
        CORE_OBJECT_CLASS_ID_LENGTH
    );
}


/* CORE_PUBLIC */ CoreClassID
Core_getClassID(CoreObjectRef o)
{
    __CORE_VALIDATE_OBJECT_RET1(o, CORE_CLASS_ID_UNKNOWN);
    CORE_DUMP_OBJ_TRACE(o, __FUNCTION__);
    
    return __Core_getObjectClassID(o);
} 



/* CORE_PROTECTED */ CoreBOOL
Core_isCoreObject(CoreObjectRef o, CoreClassID id, const char * funcName)
{
    CoreBOOL result = false;
    CoreClassID objectClassID = __Core_getObjectClassID(o);
    CoreRuntimeObject * _o = (CoreRuntimeObject *) o;
        
    if ((o != null) && 
        (CoreRuntimeClassTable[__Core_getObjectClassID(o)] != null) && 
        (objectClassID != CoreUnknownID) && 
        (objectClassID != CoreGenericID))
    {
        result = true;
    }
    
    CORE_ASSERT_RET1(
        false, 
        result,
        CORE_LOG_ASSERT, 
        "%s(): pointer %p is not a Core object", funcName, o
    );
    
    if (result)
    {
        result = (__Core_getObjectClassID(o) == id) ? true : false;
        CORE_ASSERT_RET1(
            false, 
            result, 
            CORE_LOG_ASSERT, 
            "%s(): pointer %p is not a %s", 
            funcName, o, CoreRuntimeClassTable[id]->name 
        );
    }
    
    return result;    
}


/* CORE_PROTECTED */ void
_Core_setObjectClassID(CoreObjectRef o, CoreClassID classID)
{
    CoreRuntimeObject * _o = (CoreRuntimeObject *) o;
    CoreBitfield_setValue(
        _o->info,
        CORE_OBJECT_CLASS_ID_START,
        CORE_OBJECT_CLASS_ID_LENGTH,
        classID
    );
}


/* CORE_PROTECTED */ void * 
_Core_getISAForClassID(CoreClassID classID)
{
    return (void *) CoreRuntimeClassTable[classID]; 
}


/* CORE_PROTECTED */ CoreAllocatorRef
Core_getAllocator(CoreObjectRef o)
{
    void * result = null;
    CoreRuntimeObject * _o = (CoreRuntimeObject *) o;
    CoreINT_U32 allocType = CoreBitfield_getValue(
        _o->info, 
        CORE_OBJECT_ALLOCATOR_START,
        CORE_OBJECT_ALLOCATOR_LENGTH
    );
    
    if (allocType == CORE_OBJECT_CUSTOM_ALLOCATOR)
    {
        result = (void *) ((CoreINT_U8 *)_o - sizeof(CoreAllocatorRef));
    }
    else
    {
        result = (void *) CORE_ALLOCATOR_SYSTEM;
    }
    
    return (CoreAllocatorRef) result;
}


/* CORE_PUBLIC */ CoreBOOL
Core_initialize(void)
{
    static CoreBOOL done = false;
    CoreBOOL result = false;
    
    if (!done)
    {
        done = true;
        
        CoreRuntimeClassTableSize = 256;
        CoreRuntimeClassTable = (const CoreClass **) calloc(
            CoreRuntimeClassTableSize,
            sizeof(CoreClass *)
        );
        if (CoreRuntimeClassTable != null)
        {
            CoreBase_initialize();
            
            // Now 2 basic types: the unknown and the root
            CoreUnknownID = CoreRuntime_registerClass(&__CoreUnknownClass);
            CoreGenericID = CoreRuntime_registerClass(&__CoreGenericClass);
            
            // Allocator needs to be done right after.
            CoreAllocator_initialize();
            
            // and now all the others...
            CoreString_initialize();
            CoreNotificationCenter_initialize();
            CoreData_initialize();
            CoreArray_initialize();
            CoreDictionary_initialize();
            CoreSet_initialize();
            CoreRunLoop_initialize();
            CoreMessagePort_initialize();
            
            result = true;
        }
    }
    
    return result;    
}


/* CORE_PROTECTED */ CoreObjectRef
CoreRuntime_createObject(
    CoreAllocatorRef allocator,
    CoreINT_U32 classID,
    CoreINT_U32 size    
)
{
    CoreRuntimeObject * result = null;
    CoreINT_U32 finalSize = size;
    CoreAllocatorRef defaultAllocator = CoreAllocator_getDefault();
    CoreBOOL customAllocator = false;

    // check classID

    if (allocator == null)
    {
        allocator = defaultAllocator;
    }

    CORE_DUMP_MSG(
        CORE_LOG_INFO, 
        "->%s: creating object of class %u with allocator %p \n", 
        __FUNCTION__, classID,
        allocator
    );

    // Custom allocators are stored at first 4 bytes of allocated memory!
    if (allocator != CORE_ALLOCATOR_SYSTEM)
    {
        finalSize += sizeof(CoreAllocatorRef);
        customAllocator = true;
    }
    
    result = (CoreRuntimeObject *) CoreAllocator_allocate(allocator, finalSize);
    result->isa = null;
    result->info = 0;
    
    if (customAllocator)
    {
        *(CoreAllocatorRef *)((CoreINT_U8 *) result) = Core_retain(allocator);
        result = (CoreRuntimeObject *) 
            ((CoreINT_U8 *) result) + sizeof(CoreAllocatorRef);
    }
    CoreBitfield_setValue(
        result->info, 
        CORE_OBJECT_ALLOCATOR_START,
        CORE_OBJECT_ALLOCATOR_LENGTH,
        (customAllocator) 
            ? CORE_OBJECT_CUSTOM_ALLOCATOR : CORE_OBJECT_SYSTEM_ALLOCATOR
    );

    _Core_setObjectClassID(result, classID);
    result->info += (1 << CORE_OBJECT_RC_START); // set refCount to 1
    result->isa = _Core_getISAForClassID(classID);
    if (((CoreClass *)result->isa)->init != null)
    {
        ((CoreClass *)result->isa)->init(result);
    }
    
    return result;   
}


/* CORE_PROTECTED */ void
CoreRuntime_initStaticObject(CoreObjectRef o, CoreClassID classID)
{
    CoreRuntimeObject * _o = (CoreRuntimeObject *) o;
    
    CORE_ASSERT_RET0(
        (classID != CoreUnknownID) && (classID != CoreGenericID) &&
        (CoreRuntimeClassTable[classID] != null),
        CORE_LOG_ASSERT,
        "%s(): wrong classID %u", __PRETTY_FUNCTION__, classID 
    );
    
    _o->isa = _Core_getISAForClassID(classID);
    _Core_setRetainCount(o, 0);
    _Core_setObjectClassID(o, classID);
    if (CoreRuntimeClassTable[classID]->init != null)
    {
        CoreRuntimeClassTable[classID]->init(_o);
    }    
}


CORE_INLINE CoreHashCode
_Core_hash(
    CoreObjectRef me
)
{
    CoreHashCode result = 0;
    CoreRuntimeObject * _me = (CoreRuntimeObject *) me;
    CoreHashCode (*hash)(CoreObjectRef) = ((CoreClass *)_me->isa)->hash; 
    
    if (CORE_LIKELY(hash != null))
    {
        result = hash(me);
    }
    else
    {
        //result =  (CoreHashCode) (((CoreINT_U32) me >> 4) | ((CoreINT_U32) me << (32 - 4)));
        result = (CoreHashCode) me;
    }
    
    return result;    
}

/* CORE_PUBLIC */ CoreHashCode
Core_hash(CoreObjectRef me)
{
    __CORE_VALIDATE_OBJECT_RET1(me, 0);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);

    return _Core_hash(me);
}


CORE_INLINE CoreBOOL
_Core_equal(CoreObjectRef me, CoreObjectRef to)
{
    CoreBOOL result = false;
    CoreRuntimeObject * _me = (CoreRuntimeObject *) me;
    CoreBOOL (* equal)(CoreObjectRef, CoreObjectRef);
    
    equal = ((CoreClass *)_me->isa)->equal;    
    if (CORE_LIKELY(equal != null))
    {
        result = equal(me, to);
    }
    else
    {
        result = ((CoreINT_U32) me == (CoreINT_U32) to) ? true : false;
    }
    
    return result;    
}

/* CORE_PUBLIC */ CoreBOOL
Core_equal(CoreObjectRef me, CoreObjectRef to)
{
    CoreBOOL result = false;
    
    if (me == to)
    {
        result = true;
    }
    else
    {
        __CORE_VALIDATE_OBJECT_RET1(me, false);
        CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
        
        result = _Core_equal(me, to);
    }
    
    return result;    
}


CORE_INLINE CoreImmutableStringRef
_Core_getCopyOfDescription(CoreObjectRef me)
{
    CoreImmutableStringRef result = null;
    CoreRuntimeObject * _me = (CoreRuntimeObject *) me;
    CoreImmutableStringRef (* desc)(CoreObjectRef);
    
    desc = ((CoreClass *)_me->isa)->getCopyOfDescription;
    
    if (CORE_LIKELY(desc != null))
    {
        result = desc(me);
    }
    else
    {
        char s[100];
        sprintf(
            s, 
            "%s <%p> [%p]", 
            ((CoreClass *)_me->isa)->name, me, Core_getAllocator(me)
        );
        result = CoreString_createImmutableWithASCII(null, s, strlen(s));    
    }
    
    return result;
}

CORE_PUBLIC CoreImmutableStringRef 
Core_getCopyOfDescription(CoreObjectRef me)
{
    __CORE_VALIDATE_OBJECT_RET1(me, null);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);

    return _Core_getCopyOfDescription(me);
}

