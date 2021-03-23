
/*****************************************************************************
 *
 * Includes
 * 
 *****************************************************************************/

#include "CoreArray.h"
#include "CoreInternal.h"
#include "CoreRuntime.h"
#include "CoreString.h"
#include "CoreAlgorithms.h"
#include <stdlib.h>



/*****************************************************************************
 *
 * Types definitions
 * 
 ****************************************************************************/


typedef enum CoreArrayType
{
    CORE_ARRAY_IMMUTABLE        = 1,
    CORE_ARRAY_MUTABLE_DEQUE    = 2,
    CORE_ARRAY_MUTABLE_STORAGE  = 3
} CoreArrayType;


// Using this wrapper avoids "different const ..." compiler warnings!
typedef struct __CoreBucket
{
    const void * item;
} __CoreBucket;


// A generic CoreArray object...
struct __CoreArray
{
    CoreRuntimeObject core;
    CoreINT_U32 count;
//  CoreArrayCallbacks cb;    // if custom callbacks
//  struct __CoreBucket * buckets;  // if immutable
};

// ... which in fact may be allocated as one of following types:
typedef struct __CoreArray __ArrayImmutable;

typedef struct __ArrayMutable
{
    CoreRuntimeObject core;
    CoreINT_U32 count;
    CoreINT_U32 maxCapacity;
    void * storage;    
} __ArrayMutable;

typedef struct __ArrayDeque
{
    CoreINT_U32 head;
    CoreINT_U32 capacity;
    CoreINT_S32 bias;
//  struct __CoreBucket * buckets;     
} __ArrayDeque;


typedef enum CoreArrayCallbacksType
{
    CORE_ARRAY_NULL_CALLBACKS   = 1,
    CORE_ARRAY_CORE_CALLBACKS   = 2,
    CORE_ARRAY_CUSTOM_CALLBACKS = 3,
} CoreArrayCallbacksType;


const CoreArrayCallbacks CoreArrayCoreCallbacks =
{ 
    Core_retain, 
    Core_release, 
    Core_getCopyOfDescription,
    Core_equal
};

static const CoreArrayCallbacks CoreArrayNullCallbacks =
{ 
    null, 
    null, 
    null,
    null
};
	



static CoreClassID CoreArrayID = CORE_CLASS_ID_UNKNOWN;


#define CORE_ARRAY_MINIMAL_CAPACITY     8
#define CORE_ARRAY_MAX_CAPACITY     (1 << 31)
#define CORE_ARRAY_MAX_DEQUE_CAPACITY   CORE_ARRAY_MAX_CAPACITY



#define CORE_IS_ARRAY(array) CORE_VALIDATE_OBJECT(array, CoreArrayID)
#define CORE_IS_ARRAY_RET0(array) \
    do { if(!CORE_IS_ARRAY(array)) return ;} while (0)
#define CORE_IS_ARRAY_RET1(array, ret) \
    do { if(!CORE_IS_ARRAY(array)) return (ret);} while (0)



//
// Custom object's information:
//  - type of array in 0-1 bits
//  - type of callbacks on 2-3nd bit
//

#define CORE_ARRAY_TYPE_START       0
#define CORE_ARRAY_TYPE_LENGTH      2

#define CORE_ARRAY_CALLBACKS_START  2
#define CORE_ARRAY_CALLBACKS_LENGTH 2


CORE_INLINE CoreArrayType
__CoreArray_getType(CoreImmutableArrayRef me)
{
    return (CoreArrayType) CoreBitfield_getValue(
        ((const CoreRuntimeObject *) me)->info,
        CORE_ARRAY_TYPE_START,
        CORE_ARRAY_TYPE_LENGTH
    );
}

CORE_INLINE void
__CoreArray_setType(CoreImmutableArrayRef me, CoreArrayType type)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) me)->info,
        CORE_ARRAY_TYPE_START,
        CORE_ARRAY_TYPE_LENGTH,
        (CoreINT_U32) type
    );
}

CORE_INLINE CoreArrayCallbacksType
__CoreArray_getCallbacksType(CoreImmutableArrayRef me)
{
    return (CoreArrayCallbacksType) CoreBitfield_getValue(
        ((const CoreRuntimeObject *) me)->info,
        CORE_ARRAY_CALLBACKS_START,
        CORE_ARRAY_CALLBACKS_LENGTH
    );
}

CORE_INLINE void
__CoreArray_setCallbacksType(
    CoreImmutableArrayRef me, 
    CoreArrayCallbacksType type
)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) me)->info,
        CORE_ARRAY_CALLBACKS_START,
        CORE_ARRAY_CALLBACKS_LENGTH,
        (CoreINT_U32) type
    );
}


CORE_INLINE CoreArrayCallbacks *
__CoreArray_getCallbacks(CoreImmutableArrayRef me, CoreArrayType type)
{
    const CoreArrayCallbacks * result = null;
    
    switch (__CoreArray_getCallbacksType(me))
    {
        case CORE_ARRAY_NULL_CALLBACKS:
            result = &CoreArrayNullCallbacks;
            break;
        case CORE_ARRAY_CORE_CALLBACKS:
            result = &CoreArrayCoreCallbacks;
            break;
    }
    
    if (result == null)
    {
        switch (type)
        {
            case CORE_ARRAY_IMMUTABLE:
                result = (CoreArrayCallbacks *)
                    ((CoreINT_U8 *) me + sizeof(__ArrayImmutable));
                break;
            case CORE_ARRAY_MUTABLE_DEQUE:
            case CORE_ARRAY_MUTABLE_STORAGE:
                result = (CoreArrayCallbacks *)
                    ((CoreINT_U8 *) me + sizeof(__ArrayMutable));
                break;
        }
    }
    
    return (CoreArrayCallbacks *) result;
}


CORE_INLINE CoreBOOL
__CoreArray_callbacksMatchNull(const CoreArrayCallbacks * cb)
{
    CoreBOOL result = false;
    
    result = (
        (cb == null) ||
        ((cb->retain == null) &&
         (cb->release == null) &&
         (cb->getCopyOfDescription == null) &&
         (cb->equal == null))
    );
    
    return result;
}

CORE_INLINE CoreBOOL
__CoreArray_callbacksMatchCore(const CoreArrayCallbacks * cb)
{
    CoreBOOL result = false;
    
    result = (
        (cb != null) &&
        ((cb->retain == Core_retain) &&
         (cb->release == Core_release) &&
         (cb->getCopyOfDescription == Core_getCopyOfDescription) &&
         (cb->equal == Core_equal))
    );
    
    return result;
}


CORE_INLINE CoreINT_U32
__CoreArray_getSizeOfType(CoreImmutableArrayRef me, CoreArrayType type)
{
    CoreINT_U32 result = 0;
    
    switch (type)
    {
        case CORE_ARRAY_IMMUTABLE:
            result += sizeof(__ArrayImmutable);
            break;
        case CORE_ARRAY_MUTABLE_DEQUE:
            result = sizeof(__ArrayMutable);
            break;
        case CORE_ARRAY_MUTABLE_STORAGE:
            break;
    }
    
    if (__CoreArray_getCallbacksType(me) == CORE_ARRAY_CUSTOM_CALLBACKS)
    {
        result += sizeof(CoreArrayCallbacks);
    }
    
    return result;
}


// Should not be called when count = 0        
CORE_INLINE __CoreBucket *
__CoreArray_getBucketsPtr(CoreImmutableArrayRef me, CoreArrayType type)
{
    __CoreBucket * result = null;
     
    switch(type)
    {
        case CORE_ARRAY_IMMUTABLE:
            result = (__CoreBucket *) (CoreINT_U8 *) me + 
                __CoreArray_getSizeOfType(me, type);
            break;
        case CORE_ARRAY_MUTABLE_DEQUE:
        {
            __ArrayDeque * deque;
            deque = (__ArrayDeque *) ((__ArrayMutable *) me)->storage;
            result = (__CoreBucket *) ((CoreINT_U8 *) deque + 
                sizeof(__ArrayDeque) + deque->head * sizeof(__CoreBucket));
            break;
        }
    }
    
    return result;
}


// Should not be called when count = 0
CORE_INLINE __CoreBucket *
__CoreArray_getBucketAtIndex(
    CoreImmutableArrayRef me, 
    CoreIndex index
)
{
    __CoreBucket * result = null;
    CoreArrayType type = __CoreArray_getType(me);
    
    switch (type)
    {
        case CORE_ARRAY_IMMUTABLE:
        case CORE_ARRAY_MUTABLE_DEQUE:
            result = __CoreArray_getBucketsPtr(me, type) + index;
            break;
        case CORE_ARRAY_MUTABLE_STORAGE:
            break;
    }
    
    return result;
}


CORE_INLINE CoreINT_U32 
__CoreArray_getCount(CoreImmutableArrayRef me)
{
	return me->count;
}	


CORE_INLINE void 
__CoreArray_setCount(CoreImmutableArrayRef me, CoreINT_U32 newCount)
{
    ((struct __CoreArray *) me)->count = newCount;
}


CORE_INLINE CoreINT_U32
__CoreArray_getMaxCapacity(CoreImmutableArrayRef me, CoreArrayType type)
{
    CoreINT_U32 result = 0;
    
    switch (type)
    {
        case CORE_ARRAY_MUTABLE_DEQUE:
        case CORE_ARRAY_MUTABLE_STORAGE:
        {
            result = ((__ArrayMutable *)me)->maxCapacity;            
        }
    }
    
    return result;
}


CORE_INLINE CoreINT_U32
__CoreArray_roundUpCapacity(CoreINT_U32 capacity)
{
    return (capacity <= CORE_ARRAY_MINIMAL_CAPACITY)
        ? CORE_ARRAY_MINIMAL_CAPACITY
        : (capacity < 1024)
            ? (1 << (CoreINT_U32)(CoreBits_mostSignificantBit(capacity - 1) + 1))
            : (CoreINT_U32)((capacity * 3) / 2);
}


// Should not be called when count = 0
CORE_INLINE CoreINT_U32
__CoreArray_getIndexOfValue(
    CoreImmutableArrayRef me, 
    CoreRange range,
    const void * value,
    CoreBOOL reverse
)
{
    CoreINT_U32 result = CORE_INDEX_NOT_FOUND;
    CoreArrayType type = __CoreArray_getType(me);
    CoreArrayCallbacks * cb = __CoreArray_getCallbacks(me, type);
    const CoreArray_equalCallback opEqual = cb->equal;
    CoreINT_U32 from = range.offset;
    CoreINT_U32 to = range.offset + range.length;
    CoreINT_U32 diff = 1;
    CoreINT_U32 idx;
    __CoreBucket * buckets = __CoreArray_getBucketsPtr(me, type);
    
    if (reverse)
    {
        from += range.length;
        to -= range.length;
        diff = -1;
    }
        
    for (idx = from; idx != to; idx += diff)
    {
        if ((value == buckets[idx].item) || 
            ((opEqual != null) && (opEqual(value, buckets[idx].item))))
        {
            result = idx;
            break;
        }
    }
    
    return result;        
}


CORE_INLINE void
__CoreArray_ensureAddCapacity(
    CoreArrayRef me, 
    CoreINT_U32 changedCount,
    CoreArrayType type
)
{
    /*
     * Closed until TREE_STORAGE introduce.
     */     
}


CORE_INLINE void
__CoreArray_ensureRemoveCapacity(
    CoreArrayRef me, 
    CoreINT_U32 changedCount,
    CoreArrayType type
)
{
    /*
     * Closed until TREE_STORAGE introduce.
     */     
}


static void
_CoreArray_releaseValues(
    CoreArrayRef me,
    CoreRange range
)
{
    CoreArrayType type = __CoreArray_getType(me);
    CoreArrayCallbacks * cb = __CoreArray_getCallbacks(me, type);
    
    if ((cb->release != null) && (range.length > 0))
    {
        __CoreBucket * buckets;
        CoreINT_U32 idx;
        
        buckets = __CoreArray_getBucketsPtr(me, type);
        for (idx = range.offset; idx < range.offset + range.length; idx++)
        {
            cb->release(buckets[idx].item); 
            //buckets[idx].item = null; // not necessary...  
        }
    }
}


// only for Deque
static void
_CoreArray_setValues(
    CoreArrayRef me,
    CoreINT_U32 index,
    const void ** values,
    CoreINT_U32 count
)
{
    CoreArrayType type = __CoreArray_getType(me);
    CoreArrayCallbacks * cb = __CoreArray_getCallbacks(me, type);
    __CoreBucket * buckets;
    
    buckets = __CoreArray_getBucketsPtr(me, type);
    if (cb->retain != null)
    {
        CoreINT_U32 idx;
        for (idx = 0; idx < count; idx++)
        {
            buckets[idx + index].item = cb->retain(values[idx]);
        }
    }
    else
    {
        if (count == 1)
        {
            buckets[index].item = values[0];
        }
        else
        {
            memmove(buckets + index, values, count * sizeof(__CoreBucket));
        }        
    }
}


// Constant in bits used when resizing...
// 		Expanding the capacity takes place when:
//			capacity < requested count
//		or	empty room < max(4, (capacity >> EMPTY_ROOM_DIVISOR))
#define EMPTY_ROOM_DIVISOR 10

/*
 * Despite it may look like a big performance hog, it tries to optimize
 * the rearangement of the deque in the most efficient way. Especially for
 * large arrays it can save a lot. 
 * 
 * Note: valid only for deque
 */  
static CoreBOOL
_CoreArray_resizeDeque(
    CoreArrayRef me, 
    CoreRange range, 
    CoreINT_U32 count
)
{
    /*
     *  _______________________________
     *  |     |     |     |     |     |
     *  |  L  |  A  |  B  |  C  |  R  |
     *  |_____|_____|_____|_____|_____|
     * 
     * L -- length of region up to deque->head
     * A -- length in deque from its head to range.offset
     * B -- length of replaced range
     * C -- length in deque from replaced range
     * R -- length of region from end of deque to its capacity
     *
     */                                                              
    CoreBOOL result = true;
    __ArrayDeque * deque;
    __CoreBucket * buckets;
    CoreINT_U32 _count, newCount, L, A, B, C, R, minEmptyRoom;
    CoreINT_S32 countChange;
    
    deque = (__ArrayDeque *) ((__ArrayMutable *)me)->storage;
    buckets = (__CoreBucket *) ((CoreINT_U8 *) deque + sizeof(__ArrayDeque));
    _count = __CoreArray_getCount(me);
    newCount = _count - range.length + count;
    L = deque->head;
    A = range.offset;
    B = range.length;
    C = _count - A - B;
    R = deque->capacity - _count - L;
    minEmptyRoom = max(4U, (deque->capacity >> EMPTY_ROOM_DIVISOR));
    countChange = count - B;
        
    if ((deque->capacity < newCount) 
        || ((_count < newCount) && ((L + R) < minEmptyRoom)))
    {
        //
        // Inserting... reallocation needs to be done.
        //
        CoreAllocatorRef allocator = Core_getAllocator(me);
        __ArrayDeque * newDeque;
        __CoreBucket * newBuckets; 
        CoreINT_U32 capacity;
        CoreINT_U32 size;

        capacity = __CoreArray_roundUpCapacity(newCount + minEmptyRoom);
        size = sizeof(__ArrayDeque) + capacity * sizeof(__CoreBucket);
        newDeque = CoreAllocator_allocate(allocator, size);
        if (newDeque == null)
        {
            // handle out-of-memory error
            CORE_DUMP_MSG(CORE_LOG_CRITICAL, "Error! out-of-memory\n");
            result = false;
        }
        else
        {
            newBuckets = (__CoreBucket *)
                ((CoreINT_U8 *)newDeque + sizeof(__ArrayDeque));
            newDeque->head = (capacity - newCount) / 2;
            newDeque->capacity = capacity;
            newDeque->bias = 0;

            if (A > 0)
            {
				CoreINT_U32 newL = newDeque->head;
                memcpy(
                    newBuckets + newL, 
                    buckets + L, 
                    (size_t) (A * sizeof(__CoreBucket))
                );
            }

            if (C > 0)
            {
                CoreINT_U32 offsetOfC = L + A + B;
                CoreINT_U32 offsetOfNewC = newDeque->head + A + count;
                memcpy(
                    newBuckets + offsetOfNewC, 
                    buckets + offsetOfC, 
                    (size_t) (C * sizeof(__CoreBucket))
                );
            }
			
            CoreAllocator_deallocate(allocator, deque);
            ((__ArrayMutable *)me)->storage = newDeque;
        }
    }   
    else
    {
        // 
        // Reallocation of the store is not needed... now just 
        // accomodate the internal structure of the deque.
        // Note: Always moving the smaller part.
        //

        if (((countChange < 0) && (C < A)) 
            || ((countChange <= (CoreINT_S32) R) && (C < A)))
        {
            //
            // move C...
            //          deleting: C is smaller
            //          inserting: C is smaller and R has room
            //
            CoreINT_U32 offsetOfC = L + A + B;
            CoreINT_U32 offsetOfNewC = L + A + count;
			
            if (C > 0)
            {
                memmove(
                    buckets + offsetOfNewC, 
                    buckets + offsetOfC, 
                    (size_t) C * sizeof(__CoreBucket)
                );
            }
            /*if (offsetOfNewC < offsetOfC)
            {
                memset(
                    buckets + offsetOfNewC + C, 
                    0, 
                    (offsetOfC - offsetOfNewC) * sizeof(__CoreBucket)
                );
            }*/
        }
        else if ((countChange < 0) 
                || ((countChange <= (CoreINT_S32) L) && (A <= C)))
        {
            //
            // move A...
            //          deleting: A is smaller or equal
            //          inserting: A is smaller and L has room
            //
            CoreINT_S32 newL = (CoreINT_S32) L - countChange;

            deque->head = (CoreINT_U32) newL;
            if (A > 0)
            {
                memmove(
                    buckets + newL, 
                    buckets + L, 
                    (size_t) (A * sizeof(__CoreBucket))
                );
            }
            /*if (newL > oldL) 
            {
                memset(buckets + L, 0, (newL - L) * sizeof(__CoreBucket));
            }*/
        }
        else
        {
            //
            // Now must be inserting, and either:
            // 	A <= C, but L doesn't have room (R might have, but we don't care)
            // 	C < A, but R doesn't have room (L might have, but we don't care)
            // Recenter everything.
            //
            CoreINT_S32 oldBias = deque->bias;
            CoreINT_U32 newL = ((CoreINT_S32) (L + R) - countChange) / 2;
            CoreINT_U32 offsetOfC = L + A + B;
            CoreINT_U32 offsetOfNewC = (CoreINT_U32) newL + A + count; 

            deque->bias = (newL < L) ? -1 : 1;
            if (oldBias < 0)
            {
                newL = newL - newL / 2;
            }
            else if (0 < oldBias)
            {
                newL = newL + newL / 2;
            } 
            else
            {
                // Nothing
            }

            deque->head = (CoreINT_U32) newL;
            if (newL < L)
            {
                // moving to left
                if (A > 0)
                {
                    memmove(
                        buckets + newL, 
                        buckets + L, 
                        (size_t) A * sizeof(__CoreBucket)
                    );
				}
                if (C > 0)
                {
                    memmove(
                        buckets + offsetOfNewC, 
                        buckets + offsetOfC, 
                        (size_t) C * sizeof(__CoreBucket)
                    );
                }
                /*if (offsetOfC > offsetOfNewC) 
                {
                    memset(
                        buckets + offsetOfNewC + C, 
                        0, 
                        (offsetOfC - offsetOfNewC) * sizeof(__CoreBucket)
                    );
                }*/
            }
            else
            {
                // moving to right
                if (C > 0)
                {
                    memmove(
                        buckets + offsetOfNewC, 
                        buckets + offsetOfC, 
                        (size_t) C * sizeof(__CoreBucket)
					);
                }
                if (A > 0)
                {
                    memmove(
                        buckets + newL, 
                        buckets + L, 
                        (size_t) A * sizeof(__CoreBucket)
                    );
                }
                /*if (newL > oldL) 
                {
                    memset(
                        deque->buckets + L, 
                        0, 
                        (newL - L) * sizeof(__CoreBucket)
                    );
                }*/
            }
        }
    }

    return result;
}		

#undef EMPTY_ROOM_DIVISOR


static CoreBOOL
_CoreArray_replaceValues(
    CoreArrayRef me,
    CoreRange range,
    const void ** values,
    CoreINT_U32 count
)
{
    CoreBOOL result = true;
    CoreArrayType type = __CoreArray_getType(me);
    __ArrayMutable * _me = (__ArrayMutable *) me;
    CoreINT_U32 _count = __CoreArray_getCount(me);
    CoreINT_U32 newCount = _count + (count - range.length);

    if ((newCount > __CoreArray_getMaxCapacity(me, type))
        || (type != CORE_ARRAY_MUTABLE_DEQUE)) // currently only for deque 
    {
        result = false;
    }
    else
    {
        // Check whether deletion is to happen... release values first.
        if (range.length > 0)
        {
            _CoreArray_releaseValues(me, range);	
        }
        
        if (CORE_UNLIKELY(_me->storage == null))
        {
            __ArrayDeque * deque = null;
            CoreINT_U32 capacity;
            CoreINT_U32 size;
            
            capacity = __CoreArray_roundUpCapacity(1);
            size = sizeof(__ArrayDeque) + capacity * sizeof(__CoreBucket);
            deque = CoreAllocator_allocate(Core_getAllocator(me), size);
            if (deque != null)
            {
                deque->capacity = capacity;
                deque->head = capacity / 2;
                deque->bias = 0;
                _me->storage = deque;
            }
            else
            {
                CORE_DUMP_MSG(CORE_LOG_CRITICAL, "Error! out-of-memory "
                " when allocating %d bytes.\n", size);
            }
        }
        else
        {
            // 
            // Now if number of new values is different from the number 
            // of deleted old values, do a size accomodation.
            //
            if (newCount != _count)
            {
                (newCount < me->count)
                    ? __CoreArray_ensureRemoveCapacity(me, _count - newCount, type)
                    : __CoreArray_ensureAddCapacity(me, newCount - _count, type);
                    			
                if (!_CoreArray_resizeDeque(me, range, count))
                {
                    result = false;
                }
            }
        }
    
        if (count > 0)
        {
            _CoreArray_setValues(me, range.offset, values, count);
        }
        __CoreArray_setCount(me, newCount);
    }
    

    return result;
}


// Optimized function for adding on the first index.
static CoreBOOL 
_CoreArray_addFirst(CoreArrayRef me, const void * value)
{
    CoreBOOL result = false;
    CoreArrayType type = __CoreArray_getType(me);
    
    switch (type)
    {
        case CORE_ARRAY_MUTABLE_DEQUE:
        {
            __ArrayDeque * deque;
            
            deque = (__ArrayDeque *) ((__ArrayMutable *) me)->storage;
            if (deque->head > 0)
            {
                CoreArrayCallbacks * cb = __CoreArray_getCallbacks(me, type);
                __CoreBucket * buckets;
                
                if (cb->retain != null)
                {
                    cb->retain((void *) value);
                }
                buckets = __CoreArray_getBucketsPtr(me, type);
                buckets[0].item = value;
                deque->head--;
                __CoreArray_setCount(me, __CoreArray_getCount(me) + 1);
                __CoreArray_ensureAddCapacity(me, 1, type);
                result = true;
            }
            else
            {
                result = _CoreArray_replaceValues(
                    me, 
                    CoreRange_create(0, 0),
                    &value,
                    1
                );
            } 
            break;
        }
        case CORE_ARRAY_MUTABLE_STORAGE:
            break;
    }
    
    return result;
}


// Optimized function for appending.
static CoreBOOL 
_CoreArray_addLast(CoreArrayRef me, const void * value)
{
    CoreBOOL result = false;
    CoreArrayType type = __CoreArray_getType(me);
        
    switch (type)
    {
        case CORE_ARRAY_MUTABLE_DEQUE:
        {
            __ArrayDeque * deque;
            CoreINT_U32 _count = __CoreArray_getCount(me);
            
            deque = (__ArrayDeque *) ((__ArrayMutable *) me)->storage;
            if (deque->head + _count < deque->capacity)
            {
                CoreArrayCallbacks * cb = __CoreArray_getCallbacks(me, type);
                __CoreBucket * buckets;
                
                if (cb->retain != null)
                {
                    cb->retain((void *) value);
                }
                buckets = __CoreArray_getBucketsPtr(me, type);
                buckets[_count].item = value;
                __CoreArray_setCount(me, _count + 1);
                __CoreArray_ensureAddCapacity(me, 1, type);
                result = true;
            }
            else
            {
                result = _CoreArray_replaceValues(
                    me, 
                    CoreRange_create(_count, 0),
                    &value,
                    1
                );
            } 
            break;
        }
        case CORE_ARRAY_MUTABLE_STORAGE:
            break;
    }
    
    return result;
}


// Optimized function for removing from the first index.
static const void * 
_CoreArray_removeFirst(CoreArrayRef me)
{
    const void * result = null;
    CoreArrayType type = __CoreArray_getType(me);
        
    switch (type)
    {
        case CORE_ARRAY_MUTABLE_DEQUE:
        {
            CoreINT_U32 _count = __CoreArray_getCount(me);
            if (_count > 0)
            {
                __ArrayDeque * deque;
                __CoreBucket * buckets;
                CoreArrayCallbacks * cb;
                
                deque = (__ArrayDeque *) ((__ArrayMutable *) me)->storage;
                buckets = __CoreArray_getBucketsPtr(me, type);
                cb = __CoreArray_getCallbacks(me, type);
                result = buckets[0].item;
                
                if (cb->release != null)
                {
                    cb->release(result);
                }
                deque->head++;
                __CoreArray_setCount(me, _count - 1);
            }
            break;
        }
        case CORE_ARRAY_MUTABLE_STORAGE:
            __CoreArray_ensureRemoveCapacity(me, 1, type);
            break;
    }
    
    return result;
}


// Optimized function for removing last item.
static const void * 
_CoreArray_removeLast(CoreArrayRef me)
{
    const void * result = null;
    CoreArrayType type = __CoreArray_getType(me);
        
    switch (type)
    {
        case CORE_ARRAY_MUTABLE_DEQUE:
        {
            CoreINT_U32 _count = __CoreArray_getCount(me);
            if (_count > 0)
            {
                __ArrayDeque * deque;
                __CoreBucket * buckets;
                CoreArrayCallbacks * cb;

                deque = (__ArrayDeque *) ((__ArrayMutable *) me)->storage;
                buckets = __CoreArray_getBucketsPtr(me, type);
                cb = __CoreArray_getCallbacks(me, type);
                result = buckets[_count - 1].item;
                
                if (cb->release != null)
                {
                    cb->retain(result);
                }
                __CoreArray_setCount(me, _count - 1);
            }
            break;
        }
        case CORE_ARRAY_MUTABLE_STORAGE:
            __CoreArray_ensureRemoveCapacity(me, 1, type);
            break;

    }
    
    return result;
}



static CoreHashCode 
__CoreArray_hash(CoreObjectRef me)
{
    return __CoreArray_getCount((CoreImmutableArrayRef) me);
}


static CoreBOOL
__CoreArray_equal(CoreObjectRef me, CoreObjectRef to)
{
    CoreBOOL result = false;
    CORE_IS_ARRAY_RET1(me, false);
    CORE_IS_ARRAY_RET1(to, false);
    
    if (me == to)
    {
        result = true;
    }
    else
    {
        CoreImmutableArrayRef _me = (CoreImmutableArrayRef) me;
        CoreImmutableArrayRef _to = (CoreImmutableArrayRef) to;
    }
    
    return result;    
}


static CoreImmutableStringRef
__CoreArray_getCopyOfDescription(CoreObjectRef me)
{
    return null;
}


static void
__CoreArray_cleanup(CoreObjectRef me)
{
    struct __CoreArray * _me = (struct __CoreArray *) me;

    _CoreArray_releaseValues(
        _me, 
        CoreRange_create(0, __CoreArray_getCount(_me))
    );
    
    switch(__CoreArray_getType(_me))
    {
        case CORE_ARRAY_MUTABLE_DEQUE:
        {
            __ArrayMutable * __me = (__ArrayMutable *) _me;
            
            if (__me->storage != null)
            {
                CoreAllocator_deallocate(
                    Core_getAllocator(me),
                    __me->storage
                );
            }
            break;
        }
        case CORE_ARRAY_MUTABLE_STORAGE:
            break;
    }
}




static const CoreClass __CoreArrayClass =
{
    0x00,                            // version
    "CoreArray",                     // name
    NULL,                            // init
    NULL,                            // copy
    __CoreArray_cleanup,             // cleanup
    __CoreArray_equal,               // equal
    __CoreArray_hash,                // hash
    __CoreArray_getCopyOfDescription // getCopyOfDescription
};

/* CORE_PROTECTED */ void
CoreArray_initialize(void)
{
    CoreArrayID = CoreRuntime_registerClass(&__CoreArrayClass);
}

/* CORE_PUBLIC */ CoreClassID
CoreArray_getClassID(void)
{
    return CoreArrayID;
}



static struct __CoreArray *
__CoreArray_init(
    CoreAllocatorRef allocator,
    CoreINT_U32 capacity,
    const CoreArrayCallbacks * callbacks,
    CoreBOOL isMutable
)
{
    struct __CoreArray * result = null;
    CoreINT_U32 size = 0;
    CoreArrayType type;
    CoreArrayCallbacksType cbType;
    
    if (isMutable)
    {
        size += sizeof(__ArrayMutable);
        if (capacity < CORE_ARRAY_MAX_DEQUE_CAPACITY)
        {
            type = CORE_ARRAY_MUTABLE_DEQUE;
        }
        else
        {
            type = CORE_ARRAY_MUTABLE_STORAGE;
        }
    }
    else
    {
        type = CORE_ARRAY_IMMUTABLE;
        size += sizeof(__ArrayImmutable);
        size += capacity * sizeof(__CoreBucket);
    }

    if (__CoreArray_callbacksMatchNull(callbacks))
    {
        callbacks = &CoreArrayNullCallbacks;
        cbType = CORE_ARRAY_NULL_CALLBACKS;
    }
    else if (__CoreArray_callbacksMatchCore(callbacks))
    {
        callbacks = &CoreArrayCoreCallbacks;
        cbType = CORE_ARRAY_CORE_CALLBACKS;    
    }
    else
    {
        cbType = CORE_ARRAY_CUSTOM_CALLBACKS;
        size += sizeof(CoreArrayCallbacks);
    }
    
    result = (struct __CoreArray *)
        CoreRuntime_createObject(allocator, CoreArrayID, size);
    if (result != null)
    {
        __CoreArray_setType(result, type);
        __CoreArray_setCallbacksType(result, cbType);
        __CoreArray_setCount(result, 0);
        if (cbType == CORE_ARRAY_CUSTOM_CALLBACKS)
        {
            CoreArrayCallbacks * cb = __CoreArray_getCallbacks(result, type);
            *cb = *callbacks;                
        }
        
        switch (type)
        {
            case CORE_ARRAY_MUTABLE_DEQUE:
            {
                __ArrayMutable * me = (__ArrayMutable *) result;
                me->maxCapacity = (capacity > 0) 
                    ? capacity : CORE_ARRAY_MAX_CAPACITY;
                me->storage = null;
                break;
            }
        }
    }
    
    return result;   
}


/* CORE_PUBLIC */ CoreArrayRef
CoreArray_create(
    CoreAllocatorRef allocator,
    CoreINT_U32 capacity,
    const CoreArrayCallbacks * callbacks
)
{
    return __CoreArray_init(
        allocator,
        capacity,
        callbacks,
        true
    );
}


/* CORE_PUBLIC */ CoreImmutableArrayRef
CoreArray_createImmutable(
    CoreAllocatorRef allocator,
    const void ** values,
    CoreINT_U32 count,
    const CoreArrayCallbacks * callbacks
)
{
    struct __CoreArray * result = null;
    CoreArrayCallbacks * cb = null;
    __CoreBucket * buckets = null;
    
    CORE_ASSERT_RET1(
        null,
        (values != null) || (count == 0),
        CORE_LOG_ASSERT,
        "%s(): values cannot be NULL when count %u is > 0",
        __PRETTY_FUNCTION__
    );
    
    result = __CoreArray_init(
        allocator,
        count,
        callbacks,
        false
    );
    if (result != null)
    {
        buckets = __CoreArray_getBucketsPtr(result, CORE_ARRAY_IMMUTABLE);
        cb = __CoreArray_getCallbacks(result, CORE_ARRAY_IMMUTABLE);
        if (cb->retain != null)
        {
            CoreINT_U32 idx;
            for (idx = 0; idx < count; idx++)
            {
                buckets[idx].item = cb->retain(values[idx]);
            }
        }
        else
        {
            memcpy(buckets, values, count * sizeof(__CoreBucket));
        }
        
        __CoreArray_setCount(result, count);
    }
    
    return (CoreImmutableArrayRef) result;
}


/* CORE_PUBLIC */ CoreArrayRef
CoreArray_createCopy(
    CoreAllocatorRef allocator,
    CoreImmutableArrayRef array,
    CoreINT_U32 capacity
)
{
    struct __CoreArray * result = null;
    CoreArrayCallbacks * cb = null;
    CoreINT_U32 count = 0;
        
    CORE_IS_ARRAY_RET1(array, null);
    
    count = __CoreArray_getCount(array);
    
    CORE_ASSERT_RET1(
        null,
        (capacity == 0) || (capacity >= count),
        CORE_LOG_ASSERT,
        "%s(): capacity [%u] must be either 0 or >= array.count [%u]",
        __PRETTY_FUNCTION__, capacity, count
    );

    cb = __CoreArray_getCallbacks(array, __CoreArray_getType(array));
    result = __CoreArray_init(allocator, capacity, cb, true);
    if (result != null)
    {
        CoreINT_U32 idx;
        for (idx = 0; idx < count; idx++)
        {
            const void * v = __CoreArray_getBucketAtIndex(array, idx)->item;
            CoreArray_addValue(result, v);
        }
    }
    
    return (CoreArrayRef) result;
}


/* CORE_PUBLIC */ CoreImmutableArrayRef
CoreArray_createImmutableCopy(
    CoreAllocatorRef allocator,
    CoreImmutableArrayRef array
)
{
    struct __CoreArray * result = null;
    CoreArrayCallbacks * cb = null;
    CoreINT_U32 count = 0;
            
    CORE_IS_ARRAY_RET1(array, null);
    
    count = __CoreArray_getCount(array);
    cb = __CoreArray_getCallbacks(array, __CoreArray_getType(array));
    result = __CoreArray_init(allocator, count, cb, false);
    if (result != null)
    {
        __CoreBucket * buckets;
        
        buckets = __CoreArray_getBucketsPtr(result, CORE_ARRAY_IMMUTABLE);
        if (cb->retain != null)
        {
            CoreINT_U32 idx;
            for (idx = 0; idx < count; idx++)
            {
                const void * v = __CoreArray_getBucketAtIndex(array, idx)->item;
                buckets[idx].item = cb->retain(v);
            }
        }
        else
        {
            CoreINT_U32 idx;
            for (idx = 0; idx < count; idx++)
            {
                 const void * v = __CoreArray_getBucketAtIndex(array, idx)->item;
                 buckets[idx].item = v;
            }
        }
        
        __CoreArray_setCount(result, count);
    }
    
    return result;
}



/* CORE_PUBLIC */ CoreINT_U32
CoreArray_getCount(CoreImmutableArrayRef me)
{
    CORE_IS_ARRAY_RET1(me, 0);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    
    return __CoreArray_getCount(me);
}


/* CORE_PUBLIC */ CoreINT_U32
CoreArray_getCountOfValue(CoreImmutableArrayRef me, const void * value)
{
    CoreINT_U32 result = 0;
    CoreINT_U32 idx, n;
    CoreArrayType type;
    CoreArrayCallbacks * cb;
    CoreArray_equalCallback opEqual;

    CORE_IS_ARRAY_RET1(me, 0);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);

    type = __CoreArray_getType(me);
    cb = __CoreArray_getCallbacks(me, type);
    opEqual = cb->equal;
    for (idx = 0, n = __CoreArray_getCount(me); idx < n; idx++)
    {
        const void * v = __CoreArray_getBucketAtIndex(me, idx)->item;
        if ((v == value) 
            || ((opEqual != null) && (opEqual(v, value))))
        {
            result++;
        }
    }
    
    return result;    
}


/* CORE_PUBLIC */ const void *
CoreArray_getValueAtIndex(CoreImmutableArrayRef me, CoreINT_U32 index)
{
    CORE_IS_ARRAY_RET1(me, null);
    CORE_ASSERT_RET1(
        null,
        (index < __CoreArray_getCount(me)),
        CORE_LOG_ASSERT,
        "%s(): index %u is out of bounds", 
        __PRETTY_FUNCTION__, index
    );
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    
    return __CoreArray_getBucketAtIndex(me, index)->item;
}


/* CORE_PUBLIC */ CoreINT_U32
CoreArray_getFirstIndexOfValue(
    CoreImmutableArrayRef me, 
    CoreRange range,
    const void * value
)
{
    CORE_IS_ARRAY_RET1(me, CORE_INDEX_NOT_FOUND);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        CORE_INDEX_NOT_FOUND,
        range.offset + range.length <= __CoreArray_getCount(me),
        CORE_LOG_ASSERT,
        "%s(): parameter range out of bounds", __PRETTY_FUNCTION__
    );
    
    return __CoreArray_getIndexOfValue(me, range, value, false);
}


/* CORE_PUBLIC */ CoreINT_U32
CoreArray_getLastIndexOfValue(
    CoreImmutableArrayRef me, 
    CoreRange range,
    const void * value
)
{
    CORE_IS_ARRAY_RET1(me, CORE_INDEX_NOT_FOUND);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        CORE_INDEX_NOT_FOUND,
        (__CoreArray_getType(me) != CORE_ARRAY_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET1(
        CORE_INDEX_NOT_FOUND,
        range.offset + range.length <= __CoreArray_getCount(me),
        CORE_LOG_ASSERT,
        "%s(): parameter range out of bounds", __PRETTY_FUNCTION__
    );
    
    return __CoreArray_getIndexOfValue(me, range, value, true);
}


/* CORE_PROTECTED */ CoreINT_U32
_CoreArray_iterate(
    CoreImmutableArrayRef me, 
    __CoreIteratorState * state,
    const void ** buffer,
    CoreINT_U32 count
)
{
    CoreINT_U32 result = 0;
    
    switch (__CoreArray_getType(me))
    {
        case CORE_ARRAY_IMMUTABLE:
        case CORE_ARRAY_MUTABLE_DEQUE:
        {
            CoreINT_U32 _count = __CoreArray_getCount(me);
            if ((state->state == 0) && (count > 0))
            {
                state->state = 1;
                state->items = (const void **) __CoreArray_getBucketsPtr(
                    me, __CoreArray_getType(me)
                );
                result = _count;
            }
            break;
        }
        case CORE_ARRAY_MUTABLE_STORAGE:
        default:
            break;
    }
    
    return result;
} 


/* CORE_PUBLIC */ CoreBOOL
CoreArray_isFull(CoreArrayRef me)
{
    CoreBOOL result = false;
    
    CORE_IS_ARRAY_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    
    result = (__CoreArray_getCount(me) == 
              __CoreArray_getMaxCapacity(me, __CoreArray_getType(me)));
    
    return result;
}


/* CORE_PUBLIC */ void
CoreArray_copyValues(
    CoreImmutableArrayRef me, 
    CoreRange range, 
    void ** values
)
{
    CORE_IS_ARRAY_RET0(me);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET0(
        range.offset + range.length <= __CoreArray_getCount(me),
        CORE_LOG_ASSERT,
        "%s(): parameter range out of bounds", __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET0(
        (values != null),
        CORE_LOG_ASSERT,
        "%s(): values %p is null!",
        __PRETTY_FUNCTION__
    );

    switch (__CoreArray_getType(me))
    {
        case CORE_ARRAY_IMMUTABLE:
        case CORE_ARRAY_MUTABLE_DEQUE:
            memmove(
                values,
                __CoreArray_getBucketsPtr(me, __CoreArray_getType(me)) 
                    + range.offset,
                range.length * sizeof(__CoreBucket)
            );
            break;
        case CORE_ARRAY_MUTABLE_STORAGE:
            break;
    }
}


/* CORE_PUBLIC */ CoreBOOL
CoreArray_addValue(CoreArrayRef me, const void * value)
{
    CoreBOOL result;
    __ArrayMutable * _me;
    
    CORE_IS_ARRAY_RET1(me, false);    
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreArray_getType(me) != CORE_ARRAY_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );

    // Try an optimized variant when the mutable storage is already allocated.
    _me = (__ArrayMutable *) me;
    if (CORE_LIKELY(_me->storage != null))
    {
        result = _CoreArray_addLast(me, value);
    }
    else
    {
        result = _CoreArray_replaceValues(
            me, 
            CoreRange_create(__CoreArray_getCount(me), 0),
            &value,
            1
        );
    }
    
    return result;
}


/* CORE_PUBLIC */ CoreBOOL
CoreArray_insertValueAtIndex(
    CoreArrayRef me, 
    CoreINT_U32 index, 
    const void * value
)
{
    CoreBOOL result;
    __ArrayMutable * _me;
        
    CORE_IS_ARRAY_RET1(me, false);    
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreArray_getType(me) != CORE_ARRAY_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET1(
        false,
        (index <= __CoreArray_getCount(me)), 
        CORE_LOG_ASSERT,
        "%s(): index (%u) out of bounds!",
        __PRETTY_FUNCTION__, index
    );
    
    // Try an optimized variant when the mutable storage is already allocated.
    _me = (__ArrayMutable *) me;
    if (CORE_LIKELY(_me->storage != null))
    {
        if (index == 0)
        {
            result = _CoreArray_addFirst(me, value);
        }
        else if (index == __CoreArray_getCount(me))
        {
            result = _CoreArray_addLast(me, value);
        }
        else
        {
            result = _CoreArray_replaceValues(
                me, 
                CoreRange_create(index, 0),
                &value,
                1
            );
        }        
    }
    else
    {
        result = _CoreArray_replaceValues(
            me, 
            CoreRange_create(index, 0),
            &value,
            1
        );
    }
    
    return result;
}


/* CORE_PUBLIC */ CoreBOOL
CoreArray_removeValueAtIndex(
    CoreArrayRef me, 
    CoreINT_U32 index
)
{
    CoreBOOL result = false;
    
    CORE_IS_ARRAY_RET1(me, false);    
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreArray_getType(me) != CORE_ARRAY_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET1(
        false,
        (index < __CoreArray_getCount(me)), 
        CORE_LOG_ASSERT,
        "%s(): index (%u) out of bounds!",
        __PRETTY_FUNCTION__, index
    );
        
    if (index == 0)
    {
        result = (_CoreArray_removeFirst(me) != null) ? true : false;
    }
    else if (index == __CoreArray_getCount(me) - 1)
    {
        result = (_CoreArray_removeLast(me) != null) ? true : false;
    }
    else
    {
        result = _CoreArray_replaceValues(
            me, 
            CoreRange_create(index, 1),
            null,
            0
        );
    }
    
    return result;
}


/* CORE_PUBLIC */ const void *
CoreArray_setValueAtIndex(
    CoreArrayRef me, 
    CoreINT_U32 index, 
    const void * value
)
{
    const void * result;
    CoreArrayCallbacks * cb;
    __CoreBucket * bucket;
        
    CORE_IS_ARRAY_RET1(me, false);    
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreArray_getType(me) != CORE_ARRAY_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET1(
        false,
        (index < __CoreArray_getCount(me)), 
        CORE_LOG_ASSERT,
        "%s(): index (%u) out of bounds!",
        __PRETTY_FUNCTION__, index
    );
    
    cb = __CoreArray_getCallbacks(me, __CoreArray_getType(me));
    bucket = __CoreArray_getBucketAtIndex((CoreImmutableArrayRef) me, index);
    result = bucket->item;
    if (cb->release != null)
    {
        cb->release(result);
    }
    
    bucket->item = value;
    if (cb->retain != null)
    {
        (void) cb->retain(bucket);
    }
    
    return result;   
}


/* CORE_PUBLIC */ CoreBOOL
CoreArray_replaceValues(
    CoreArrayRef me,
    CoreRange range,
    const void ** values,
    CoreINT_U32 count    
)
{
    CORE_IS_ARRAY_RET1(me, false);    
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreArray_getType(me) != CORE_ARRAY_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET1(
        false,
        range.offset + range.length <= __CoreArray_getCount(me),
        CORE_LOG_ASSERT,
        "%s(): parameter range out of bounds", __PRETTY_FUNCTION__
    );
    
    return _CoreArray_replaceValues(me, range, values, count);
}


/* CORE_PUBLIC */ void
CoreArray_clear(CoreArrayRef me)
{
    CORE_IS_ARRAY_RET0(me);    
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET0(
        (__CoreArray_getType(me) != CORE_ARRAY_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    
    _CoreArray_releaseValues(
        me, 
        CoreRange_create(0, __CoreArray_getCount(me))
    );
    __CoreArray_setCount(me, 0);
    
}
    

/* CORE_PUBLIC */ void
CoreArray_applyFunction(
    CoreImmutableArrayRef me,
    CoreRange range,
    CoreArrayApplyFunction map,
    void * context    
)
{
    CoreINT_U32 idx, n;
    
    CORE_IS_ARRAY_RET0(me);    
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET0(
        map != null,
        CORE_LOG_ASSERT,
        "%s(): apply function cannot be null!",
        __PRETTY_FUNCTION__
    );
    
    for (idx = range.offset, n = range.offset + range.length; idx < n; idx++)
    {
        const void * item = __CoreArray_getBucketAtIndex(me, idx)->item;
        map(item, context);
    }
}


/* CORE_PUBLIC */ void
CoreArray_sortValues(
    CoreArrayRef me,
    CoreRange range,
    CoreComparatorFunction cmp
)
{
    CORE_IS_ARRAY_RET0(me);    
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET0(
        (__CoreArray_getType(me) != CORE_ARRAY_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET0(
        range.offset + range.length <= __CoreArray_getCount(me),
        CORE_LOG_ASSERT,
        "%s(): parameter range out of bounds", __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET0(
        cmp != null,
        CORE_LOG_ASSERT,
        "%s(): comparator function cannot be null!",
        __PRETTY_FUNCTION__
    );

    if (range.length > 1)
    {
        CoreArrayType type = __CoreArray_getType(me);
        switch (type)
        {
            case CORE_ARRAY_MUTABLE_DEQUE:
            {
                __CoreBucket * bucketPtr = __CoreArray_getBucketsPtr(me, type);
                
                bucketPtr += range.offset;
                _Core_quickSort_obj((const void **) bucketPtr, 0, range.length, cmp);
                //_Core_quickSort(bucketPtr, range.length, sizeof(void *), cmp);
                
                break;
            }
        }    
    }    
}


char *
_CoreArray_copyDescription(CoreImmutableArrayRef me)
{
    CoreCHAR_8 * result;
    CoreINT_U32 size = 80;
    __CoreBucket * buckets = null;
    CoreINT_U32 capacity = me->count;
    CoreINT_U32 head = 0;
    CoreCHAR_8 s[80] = { 0 };
    CoreINT_U32 idx;
    
    switch (__CoreArray_getType(me))
    {
        case CORE_ARRAY_MUTABLE_DEQUE:
        {
            __ArrayDeque * deque;
            deque = (__ArrayDeque *) ((__ArrayMutable *)me)->storage;
            buckets = (__CoreBucket *) ((CoreINT_U8 *) deque + sizeof(__ArrayDeque));
            size += deque->capacity * 50;
            capacity = deque->capacity;
            head = deque->head;
            break;
        }
        case CORE_ARRAY_IMMUTABLE:
            size += me->count * 50;
            buckets = (__CoreBucket *) ((CoreINT_U8 *) me + sizeof(__ArrayImmutable));
            break;
    }
    result = malloc(size);
    sprintf(s, "CoreArray <%p> :\n{\tcount = %u\n", me, me->count);
    strcat(result, s);
    for (idx = 0; idx < capacity; idx++)
    {
        sprintf(
            s, "\tentry[%u]: %p %s\n", 
            idx, 
            buckets[idx].item,
            (idx == head) ? "<-- head" : ""
        );
        strcat(result, s);
    }
    sprintf(s, "\n}");
    strcat(result, s);
    
    return result;
}

