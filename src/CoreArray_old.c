


typedef enum CoreArrayType
{
    CORE_ARRAY_IMMUTABLE        = 1,
    CORE_ARRAY_FIXED_MUTABLE    = 2,
    CORE_ARRAY_MUTABLE_DEQUE    = 3,
    CORE_ARRAY_MUTABLE_STORAGE  = 4
} CoreArrayType;

typedef struct __CoreBucket
{
    const void * item;
} __CoreBucket;

struct __CoreArray
{
    CoreRuntimeObject core;
    CoreINT_U32	count;
};

typedef struct __CoreArray __ArrayImmutable;

typedef struct __ArrayFixedMutable
{
    CoreRuntimeObject core;
    CoreINT_U32	count;
    CoreINT_U32 capacity;    
} __ArrayFixedMutable;

typedef struct __ArrayDeque
{
    CoreINT_U32 head;
    CoreINT_U32 capacity;
    CoreINT_U32 maxCapacity;
    CoreINT_U32 bias;
} __ArrayDeque;

typedef struct __ArrayMutable
{
    CoreRuntimeObject core;
    CoreINT_U32	count;
    void * storage;    
} __ArrayMutable;


typedef enum CoreArrayCallbacks
{
    CORE_ARRAY_NULL_CALLBACKS   = 1,
    CORE_ARRAY_CORE_CALLBACKS   = 2,
    CORE_ARRAY_CUSTOM_CALLBACKS = 3,
} CoreArrayCallbacks;





static CoreClassID CoreArrayID = CORE_CLASS_ID_UNKNOWN;


#define CORE_ARRAY_MINIMAL_CAPACITY     4
#define CORE_ARRAY_MAXIMAL_CAPACITY     (1 << 31)



#define CORE_IS_ARRAY(array) CORE_VALIDATE_OBJECT(array, CoreArrayID)
#define CORE_IS_ARRAY_RET0(array) \
    do { if(!CORE_IS_ARRAY(array)) return ;} while (0)
#define CORE_IS_ARRAY_RET1(array, ret) \
    do { if(!CORE_IS_ARRAY(array)) return (ret);} while (0)



//
// Custom object's information:
//  - type of array in 0-1 bits
//  - type of callbacks on 2nd bit
//

#define CORE_ARRAY_TYPE_START       0
#define CORE_ARRAY_TYPE_LENGTH      2

#define CORE_ARRAY_CALLBACKS_START  2
#define CORE_ARRAY_CALLBACKS_LENGTH 1


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

CORE_INLINE CoreCallbacksType
__CoreArray_getCallbacksType(CoreImmutableArrayRef me)
{
    return (CoreCallbacksType) CoreBitfield_getValue(
        ((const CoreRuntimeObject *) me)->info,
        CORE_ARRAY_CALLBACKS_START,
        CORE_ARRAY_CALLBACKS_LENGTH
    );
}

CORE_INLINE void
__CoreArray_setCallbacksType(CoreImmutableArrayRef me, CoreArrayType type)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) me)->info,
        CORE_ARRAY_CALLBACKS_START,
        CORE_ARRAY_CALLBACKS_LENGTH,
        (CoreINT_U32) type
    );
}


CORE_INLINE CoreArrayCallbacks *
__CoreArray_getCallbacks(CoreImmutableArrayRef me)
{
    CoreArrayCallbacks * result = null;
    
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
        switch (__CoreArray_getType(me))
        {
            case CORE_ARRAY_IMMUTABLE:
                result = (CoreArrayCallBacks *)
                    ((CoreINT_U8 *) me + sizeof(__ArrayImmutable));
                break;
            case CORE_ARRAY_MUTABLE_DEQUE:
            case CORE_ARRAY_MUTABLE_STORAGE:
                result = (CoreArrayCallBacks *)
                    ((CoreINT_U8 *) me + sizeof(__ArrayMutable));
                break;
        }
    }
    
    return result;
}

CORE_INLINE CoreINT_U32
__CoreArray_getSizeOfType(CoreImmutableArrayRef me)
{
    CoreINT_U32 size = 0;
    
    switch(__CoreArray_getType(me))
    {
        case CORE_ARRAY_IMMUTABLE:
            size = sizeof(__ArrayImmutable);
            break;
        case CORE_ARRAY_FIXED_MUTABLE:
            size = sizeof(__ArrayFixedMutable)
            break;
        case CORE_ARRAY_MUTABLE_DEQUE:
        case CORE_ARRAY_MUTABLE_STORAGE:
            size = sizeof(__ArrayMutable);
            break;
    }
    if (__CoreArray_getCallbacksType(me) == CORE_ARRAY_CUSTOM_CALLBACKS)
    {
        size += sizeof(CoreArrayCallbacks);
    }
    
    return size;
}

        
CORE_INLINE __CoreBucket *
__CoreArray_getBucketsPtr(CoreImmutableArrayRef me)
{
    __CoreBucket * result = null;
     
    switch(__CoreArray_getType(me))
    {
        case CORE_ARRAY_IMMUTABLE:
        case CORE_ARRAY_FIXED_MUTABLE:
            result = (__CoreBucket *) (CoreINT_U8 *) me + 
                __CoreArray_getSizeOfType(me);
            break;
        case CORE_ARRAY_MUTABLE_DEQUE:
        {
            __ArrayDeque * deque = ((__ArrayDeque *) me)->storage;
            result = (__CoreBucket *) ((CoreINT_U8 *) deque + 
                sizeof(__ArrayDeque) + deque->head * sizeof(__CoreBucket));
            break;
        }
    }
    
    return result;
}

CORE_INLINE __CoreBucket *
__CoreArray_getBucketAtIndex(CoreImmutableArrayRef me, CoreIndex index)
{
    __CoreBucket * result = null;
    
    switch (__CoreArray_getType(me))
    {
        case CORE_ARRAY_IMMUTABLE:
        case CORE_ARRAY_FIXED_MUTABLE:
        case CORE_ARRAY_MUTABLE_DEQUE:
            result = __CoreArray_getBucketsPtr(me)[index];
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
__CoreArray_setCount(CoreArrayRef me, CoreINT_U32 newCount)
{
    me->count = newCount;
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


CORE_INLINE CoreINT_U32
__CoreArray_getIndexOfValue(
    CoreImmutableArrayRef me, 
    CoreRange range,
    const void * value,
    CoreBOOL reverse
)
{
    CoreINT_U32 result = CORE_INDEX_NOT_FOUND;
    CoreArrayCallbacks * cb = __CoreArray_getCallbacks(me);
    CoreINT_U32 from = range.offset;
    CoreINT_U32 to = range.offset + range.length;
    CoreINT_U32 diff = 1;
    
    if (reverse)
    {
        from += range.length;
        to -= range.length;
        diff = -1;
    }
        
    if (cb->equal == null)
    {
        CoreINT_U32 idx;
        CoreBucket * buckets = __CoreArray_getBucketsPtr(me);
        for (idx = from; idx != to; idx += diff)
        {
            if (value == buckets[idx])
            {
                result = idx;
                break;
            }
        }    
    }
    else
    {
        CoreINT_U32 idx;
        CoreBucket * buckets = __CoreArray_getBucketsPtr(me);
        for (idx = from; idx != to; idx += diff)
        {
            if (cb->equal(value, buckets[idx]))
            {
                result = idx;
                break;
            }
        }    
    }
    
    return result;        
}


static void
_CoreArray_releaseValues(
    CoreArrayRef me,
    CoreRange range
)
{
    CoreArrayCallbacks * cb = __CoreArray_getCallbacks(me);
    if (cb->release != null)
    {
        CoreINT_U32 idx;
        CoreBucket * buckets = __CoreArray_getBucketsPtr(me);
        
        for (idx = range.offset; idx < range.offset + range.length; idx++)
        {
            cb->release(buckets[idx].item); 
            buckets[idx].item = null; // not necessary...  
        }
    }
}


static void
_CoreArray_setValues(
    CoreArrayRef me,
    CoreINT_U32 index,
    const void ** values,
    CoreINT_U32 count
)
{
    CoreArrayCallbacks * cb = __CoreArray_getCallbacks(me);
    CoreBucket * buckets = __CoreArray_getBucketsPtr(me);
    CoreINT_U32 idx;

    if (cb->release != null)
    {
        for (idx = 0; idx < count; idx++)
        {
            buckets[idx + index].item = cb->retain(values[idx]);
        }
    }
    else
    {
        for (idx = 0; idx < count; idx++)
        {
            buckets[idx + index].item = values[idx];
        }        
    }
}


// valid only for deque
static CoreBOOL
_CoreArray_resizeDeque(
    CoreArrayRef me, 
    CoreRange range, 
    CoreINT_U32 count
)
{
    CoreBOOL result = true;
    __ArrayDeque * deque = (__ArrayDeque *) me->storage;
    CoreBucket * buckets;
    CoreINT_U32 _count = __CoreArray_getCount(me);
    CoreINT_U32 newCount = _count - range.length + count;
    CoreINT_U32 L = deque->head;
    CoreINT_U32 A = range.offset;
    CoreINT_U32 B = range.length;
    CoreINT_U32 C = (_count - range.length - range.offset);
    CoreINT_U32 R = deque->capacity - _count - deque->head;
    CoreINT_S32 countOfNewElems = count - range.length;
    CoreINT_U32 minEmptyRoom = (deque->capacity >> EMPTY_ROOM_DIVISOR);
    CoreAllocatorRef allocator = Core_getAllocator(me);
    
    buckets = (CoreBucket *) ((CoreINT_U8 *) deque) + sizeof(__ArrayDeque);
    minEmptyRoom = max(minEmptyRoom, 4U);
    if (newCount > deque->maxCapacity)
    {
        // Request for insertion cannot be proceeded because of
        // maxCapacity settings.
        result = false;
    }
    else if ((deque->capacity < newCount) 
            || ((count < newCount) && ((L + R) < minEmptyRoom)))
    {
        //
        // Inserting... reallocation needs to be done.
        //
        __ArrayDeque * newDeque;
        __CoreBucket * newBuckets; 
        CoreINT_U32 capacity;
        CoreINT_U32 size;
        CoreINT_U32 oldL = L;
        CoreINT_U32 oldC0 = oldL + A + B;
        CoreINT_U32 newL;
        CoreINT_U32 newC0;

        capacity = __CoreArray_roundUpCapacity(
            me, newCount + minEmptyRoom
        );
        size = sizeof(__ArrayDeque) + capacity * sizeof(CoreBucket);
        newL = (deque->capacity - newCount) / 2;
        newC0 = newL + A + count;
        newDeque = CoreAllocator_allocate(allocator, size);
        if (newDeque == null)
        {
            // handle out-of-memory error
            result = false;
        }
        else
        {
            newBuckets = (__CoreBucket *)
                ((CoreINT_U8 *)newDeque + sizeof(__ArrayDeque));
            newDeque->head = newL;
            newDeque->bias = 0;

            if (A > 0)
            {
				memmove(
                    newBuckets + newL, 
                    buckets + oldL, 
                    (size_t) (A * sizeof(__CoreBucket))
                );
            }

            if (C > 0)
            {
                memmove(
                    newBuckets + newC0, 
                    buckets + oldC0, 
                    (size_t) (C * sizeof(__CoreBucket))
                );
            }
			
            CoreAllocator_release(allocator, deque);
            ((__ArrayMutable *)me)->storage = newDeque;
        }
    }   
    else
    {
        // 
        // Reallocation of the store is not needed... now just 
        // accomodate the internal structure of the deque.
        //

        if (((countOfNewElems < 0) && (C < A)) 
            || ((countOfNewElems <= (CoreINT_S32) R) && (C < A)))
        {
            //
            // move C...
            //			deleting: C is smaller
            //			inserting: C is smaller and R has room
            //
            CoreINT_U32 oldC0 = L + A + B;
            CoreINT_U32 newC0 = L + A + count;
			
            if (C > 0)
            {
                memmove(
                    buckets + newC0, 
                    buckets + oldC0, 
                    (size_t) C * sizeof(__CoreBucket)
                );
            }
            /*if (newC0 < oldC0)
            {
                memset(
                    buckets + newC0 + C, 
                    0, 
                    (oldC0 - newC0) * sizeof(__CoreBucket)
                );
            }*/
        }
        else if ((countOfNewElems < 0) 
                || ((countOfNewElems <= (CoreINT_S32) L) && (A <= C)))
        {
            //
            // move A...
            //			deleting: A is smaller or equal
            //			inserting: A is smaller and L has room
            //
            CoreINT_S32 oldL = (CoreINT_S32) L;
            CoreINT_S32 newL = (CoreINT_S32) L - countOfNewElems;

            deque->head = (CoreINT_U32) newL;
            if (A > 0)
            {
                memmove(
                    buckets + newL, 
                    buckets + oldL, 
                    (size_t) (A * sizeof(__CoreBucket))
                );
            }
            /*if (newL > oldL) 
            {
                memset(
                    buckets + oldL, 
                    0, 
                    (newL - oldL) * sizeof(__CoreBucket)
                );
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
            CoreINT_S32 oldL = (CoreINT_S32) L;
            CoreINT_S32 newL = ((CoreINT_S32)(L + R) - countOfNewElems) / 2;
            CoreINT_S32 oldBias = deque->bias;
            CoreINT_U32 oldC0 = oldL + A + B;
            CoreINT_U32 newC0 = newL + A + count; 

            deque->bias = (newL < oldL) ? -1 : 1;
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
            if (newL < oldL)
            {
                if (A > 0)
                {
                    memmove(
                        buckets + newL, 
                        buckets + oldL, 
                        (size_t) A * sizeof(__CoreBucket)
                    );
				}
                if (C > 0)
                {
                    memmove(
                        buckets + newC0, 
                        buckets + oldC0, 
                        (size_t) C * sizeof(__CoreBucket)
                    );
                }
                /*if (oldC0 > newC0) 
                {
                    memset(
                        buckets + newC0 + C, 
                        0, 
                        (oldC0 - newC0) * sizeof(__CoreBucket)
                    );
                }*/
            }
            else
            {
                if (C > 0)
                {
                    memmove(
                        buckets + newC0, 
                        buckets + oldC0, 
                        (size_t) C * sizeof(__CoreBucket)
					);
                }
                if (A > 0)
                {
                    memmove(
                        buckets + newL, 
                        buckets + oldL, 
                        (size_t) A * sizeof(__CoreBucket)
                    );
                }
                /*if (newL > oldL) 
                {
                    memset(
                        deque->buckets + oldL, 
                        0, 
                        (newL - oldL) * sizeof(__CoreBucket)
                    );
                }*/
            }
        }
    }

    return result;
}		


static CoreBOOL
_CoreArray_replaceValues(
    CoreArrayRef me,
    CoreRange range,
    const void ** values,
    CoreINT_U32 count
)
{
    CoreBOOL result = false;

    // currently only for deque
    if ((__CoreArray_getType(me) == CORE_ARRAY_MUTABLE_DEQUE) && 
        (range.offset + range.length <= me->count))
    {
        CoreINT_U32 _count = __CoreArray_getCount(me);
        CoreINT_U32 newCount = _count + (count - range.length);

        //
        // Check whether deletion is to happen... release values first.
        //
        result = true;
        if (range.length > 0)
        {
            _CoreArray_releaseValues(me, range);	
        }
        
        // 
        // Now if number of new values is different from the number of deleted
        // old values, do a size accomodation.
        //
        if (newCount != _count)
        {
            (newCount < me->count)
                ? _CoreArray_ensureRemoveCapacity(me, _count - newCount)
                : _CoreArray_ensureAddCapacity(me, newCount - _count);
                			
            if (!_CoreArray_resizeDeque(me, range, count))
            {
                result = FALSE;
            }
            else
            {
                if (count > 0)
                {
                    _CoreArray_setValues(me, range.offset, values, count);
                }

                __CoreArray_setCount(me, newCount);
            }
        }
    }

    return result;
}


// Optimized function for adding to the first index.
static CoreBOOL 
_CoreArray_addFirst(CoreArrayRef me, const void * value)
{
    CoreBOOL result = false;
    
    switch (__CoreArray_getType(me))
    {
        case CORE_ARRAY_MUTABLE_DEQUE:
        {
            __ArrayDeque * deque = ((__ArrayDeque *) me)->storage;
            if (deque->head > 0)
            {
                RETAIN(me, (void *) value);
                deque->head--;
                __CoreArray_getBucketsPtr(me)[deque->head].item = value;
                me->count++;
                _CoreArray_ensureAddCapacity(me, 1);
                result = true;
            }
            else
            {
                result = _CoreArray_replaceValues(
                    me, 
                    CoreRange_Create(0, 0),
                    value,
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
    
    switch (__CoreArray_getType(me))
    {
        case CORE_ARRAY_MUTABLE_DEQUE:
        {
            __ArrayDeque * deque = ((__ArrayDeque *) me)->storage;
            if (deque->head + __CoreArray_getCount(me) < deque->capacity)
            {
                RETAIN(me, (void *) value);
                __CoreArray_getBucketsPtr(me)[deque->head].item = value;
                me->count++;
                _CoreArray_ensureAddCapacity(me, 1);
                result = true;
            }
            else
            {
                result = _CoreArray_replaceValues(
                    me, 
                    CoreRange_Create(__CoreArray_getCount(me), 0),
                    value,
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




static CoreINT_U32 
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


static char *
__CoreArray_getCopyOfDescription(CoreObjectRef me)
{
    return null;
}


static void
__CoreArray_cleanup(CoreObjectRef me)
{

}


static struct __CoreArray *
__CoreArray_init(
    CoreAllocatorRef allocator,
    CoreINT_U32 capacity,
    CoreArrayCallbacks * callbacks,
    CoreBOOL isMutable
)
{
    struct __CoreArray * result = null;
    CoreINT_U32 size = 0;
    CoreArrayType type;
    CoreBOOL customCallbacks = false;
    
    if (__CoreArray_callbacksMatchNull(callbacks))
    {
        callbacks = &CoreArrayNullCallbacks;
    }
    else if (__CoreArray_callbacksMatchCore(callbacks))
    {
        
    }
    else
    {
        customCallbacks = true;
        size += sizeof(CoreArrayCallbacks);
    }
    
    if (isMutable)
    {
        if (capacity < CORE_ARRAY_MAX_DEQUE_CAPACITY)
        {
            type = CORE_ARRAY_MUTABLE_DEQUE;
        }
        else
        {
            type = CORE_ARRAY_MUTABLE_STORAGE;
        }
        size += sizeof(__ArrayMutable);  
    }
    else
    {
        type = CORE_ARRAY_IMMUTABLE;
        size += sizeof(__ArrayImmutable) + capacity * sizeof(__CoreBucket);
    }
    
    result = CoreRuntime_CreateObject(allocator, CoreArrayID, size);
    if (result != null)
    {
        __CoreArray
    }
    
    
    
}


/* CORE_PUBLIC */ CoreArrayRef
CoreArray_Create(
    CoreAllocatorRef allocator,
    CoreINT_U32 capacity,
    CoreArrayCallbacks * callbacks
)
{
    
}





/* CORE_PUBLIC */ CoreINT_U32
CoreArray_getCount(CoreImmutableArrayRef me)
{
    CORE_IS_ARRAY_RET1(me, 0);
    CORE_DUMP_TRACE(me, __FUNCTION__);
    
    return __CoreArray_getCount(me);
}


/* CORE_PUBLIC */ const void *
CoreArray_getValueAtIndex(CoreImmutableArrayRef me)
{
    CORE_IS_ARRAY_RET1(me, null);
    CORE_ASSERT_RET1(
        null,
        (index < __CoreArray_getCount(me)),
        CORE_LOG_ASSERT,
        "%s(): index %u is out of bounds", 
        __PRETTY_FUNCTION__, length
    );
    CORE_DUMP_TRACE(me, __FUNCTION__);
    
    return __CoreArray_getBucketAtIndex(me)->item;
}


/* CORE_PUBLIC */ CoreINT_U32
CoreArray_getFirstIndexOfValue(
    CoreImmutableArrayRef me, 
    CoreRange range,
    const void * value
)
{
    CORE_IS_ARRAY_RET1(me, CORE_INDEX_NOT_FOUND);
    CORE_DUMP_TRACE(me, __FUNCTION__);
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
    CORE_DUMP_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        CORE_INDEX_NOT_FOUND,
        (__CoreArray_getType(me) != CORE_ARRAY_IMMUTABLE)
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


/* CORE_PUBLIC */ CoreBOOL
CoreArray_addValue(CoreArrayRef me, const void * value)
{
    CORE_IS_ARRAY_RET1(me, false);    
    CORE_DUMP_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreArray_getType(me) != CORE_ARRAY_IMMUTABLE)
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );

    return _CoreArray_addLast(me, value);
}


/* CORE_PUBLIC */ CoreBOOL
CoreArray_insertValueAtIndex(
    CoreArrayRef me, 
    CoreINT_U32 index, 
    const void * value
)
{
    CoreBOOL result;
    
    CORE_IS_ARRAY_RET1(me, false);    
    CORE_DUMP_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreArray_getType(me) != CORE_ARRAY_IMMUTABLE)
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
            CoreRange_Create(index, 0),
            value,
            1
        );
    }
    
    return result;
}


/* CORE_PUBLIC */ CoreBOOL
CoreArray_removeValueAtIndex(
    CoreArrayRef me, 
    CoreINT_U32 index, 
    const void * value
)
{
    CoreBOOL result;
    
    CORE_IS_ARRAY_RET1(me, false);    
    CORE_DUMP_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreArray_getType(me) != CORE_ARRAY_IMMUTABLE)
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
        
    if (index == 0)
    {
        result = _CoreArray_removeFirst(me, value);
    }
    else if (index == __CoreArray_getCount(me))
    {
        result = _CoreArray_removeLast(me, value);
    }
    else
    {
        result = _CoreArray_replaceValues(
            me, 
            CoreRange_Create(index, 1),
            null,
            0
        );
    }
    
    return result;
}

