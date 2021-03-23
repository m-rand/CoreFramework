

/*****************************************************************************
 *
 * Includes
 * 
 *****************************************************************************/
 
#include "CorePriorityQueue.h"




/*****************************************************************************
 *
 * Types definitions
 * 
 ****************************************************************************/


typedef struct 
{
	Core_retainInfoCallback retain;
	Core_releaseInfoCallback release;
	Core_getCopyOfDescriptionCallback getCopyOfDescription;
	CorePriorityQueue_comparisonCallback compare;
} CorePriorityQueueCallbacks;


/**
 * Priority queue represented as a balanced binary heap: the two
 * children of queue[n] are queue[2*n+1] and queue[2*(n+1)].  The
 * priority queue is ordered by comparator: For each node n in the
 * heap and each descendant d of n, n <= d. The element with the
 * lowest value is in queue[0], assuming the queue is nonempty.
 */
struct __CorePriorityQueue
{
    CoreRuntimeObject core;
    CoreINT_U32	count;
    CoreINT_U32 maxCapacity;
    CorePriorityQueueCallbacks * callbacks;
    const void * items;
};






/*****************************************************************************
 *
 * Callbacks
 * 
 ****************************************************************************/

static CoreComparison
__CorePriorityQueue_emptyCompare(const void * a, const void * b)
{
    CoreComparison result;
    CoreINT_U32 _a = (CoreINT_U32) a;
    CoreINT_U32 _b = (CoreINT_U32) b;
    
    if (a < b)
    {
        result = CORE_COMPARISON_LESS_THAN;
    }
    else if (a > b)
    {
        result = CORE_COMPARISON_GREATER_THAN;
    }
    else
    {
        result = CORE_COMPARISON_EQUAL;
    }
    
    return result;
}

static void *
__CorePriorityQueue_emptyRetain(const void * o)
{
    return o;
}


static void
__CorePriorityQueue_emptyRelease(const void * o)
{
    return ;
}

static CoreImmutableStringRef
__CorePriorityQueue_emptyGetCopyOfDescription(const void * o)
{
    return NULL;
}



static const CorePriorityQueueCallbacks CorePriorityQueueNullCallbacks =
{ 
    __CorePriorityQueue_emptyRetain, 
    __CorePriorityQueue_emptyRelease, 
    __CorePriorityQueue_emptyGetCopyOfDescription,
    __CorePriorityQueue_emptyCompare
};


static const CorePriorityQueueCallbacks CorePriorityQueueCoreCallbacks =
{ 
    Core_retain, 
    Core_release, 
    Core_getCopyOfDescription,
    CoreString_compare
};





CORE_INLINE CorePriorityQueueCallbacksType
__CorePriorityQueue_getCallbacksType(CorePriorityQueueRef me)
{
    return (CorePriorityQueueCallbacksType) CoreBitfield_getValue(
        ((const CoreRuntimeObject *) me)->info,
        CORE_PRIORITY_QUEUE_CALLBACKS_START,
        CORE_PRIORITY_QUEUE_CALLBACKS_LENGTH
    );
}

CORE_INLINE void
__CorePriorityQueue_setCallbacksType(
    CorePriorityQueueRef me, 
    CorePriorityQueueCallbacksType type
)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) me)->info,
        CORE_PRIORITY_QUEUE_CALLBACKS_START,
        CORE_PRIORITY_QUEUE_CALLBACKS_LENGTH,
        (CoreINT_U32) type
    );
}

CORE_INLINE CorePriorityQueueCallbacks *
__CorePriorityQueue_getCallbacks(CorePriorityQueueRef queue)
{
    return queue->callbacks;
}

CORE_INLINE void
__CorePriorityQueue_setCallbacks(
    CorePriorityQueueRef queue, 
    CorePriorityQueueCallbacks * callbacks)
{
    switch (__CorePriorityQueue_getCallbacksType(queue))
    {
        case CORE_PRIORITY_QUEUE_NULL_CALLBACKS:
        {
            queue->callbacks = &CorePriorityQueueNullCallbacks;
            break;
        }
        case CORE_PRIORITY_QUEUE_CORE_CALLBACKS:
        {
            queue->callbacks = &CorePriorityQueueCoreCallbacks;
            break;
        }
        case CORE_PRIORITY_QUEUE_CUSTOM_CALLBACKS:
        {
            CorePriorityQueueCallbacks * cb;
            cb = (CorePriorityQueueCallbacks *)
                    ((CoreINT_U8 *) result + sizeof(__CorePriorityQueue));
            *cb = *callbacks;
            queue->callbacks = cb;            
            break;
        }
        default:
            break;
    }
}

CORE_INLINE CoreBOOL
__CorePriorityQueue_callbacksMatchNull(const CorePriorityQueueCallbacks * cb)
{
    CoreBOOL result = false;
    
    result = (
        (cb == null) 
        || ((cb->retain == null) &&
            (cb->release == null) &&
            (cb->getCopyOfDescription == null) &&
            (cb->compare == null))
    );
    
    return result;
}

CORE_INLINE CoreBOOL
__CorePriorityQueue_callbacksMatchCore(const CorePriorityQueueCallbacks * cb)
{
    CoreBOOL result = false;
    
    result = (
        (cb != null) 
        && ((cb->retain == Core_retain) &&
            (cb->release == Core_release) &&
            (cb->getCopyOfDescription == Core_getCopyOfDescription) &&
            (cb->compare == CoreString_compare))
    );
    
    return result;
}



static CoreINT_U32 
__CorePriorityQueue_indexOfValue(const CorePriorityQueueRef me, const void * value)
{
    CoreINT_U32 result = CORE_INDEX_NOT_FOUND;
    CorePriorityQueueCallbacks * cb = __CorePriorityQueue_getCallbacks(me);
    CoreINT_U32 idx;

    for (idx = 0; idx < me->count; idx++)
    {
        const void * tmp = me->items[idx];

        if ((value == tmp) || cb->compare(value, tmp) == CORE_COMPARISON_EQUAL)
        {
            result = idx;
            break;
        }
    }

    return result;
}


static void 
__CorePriorityQueue_siftUp(
    CorePriorityQueueRef me, CoreINT_U32 index, const void * value)
{
    while (index > 0)
    {
        CoreINT_U32 parentIdx = (index - 1) >> 1;
        CorePriorityQueueCallbacks * cb = __CorePriorityQueue_getCallbacks(me);
        CoreComparison cmp;

        cmp = cb->compare(value, me->items[parentIdx]);
        if (cmp != CORE_COMPARISON_LESS_THAN)
        {
            break;
        }
        me->items[index] = me->items[parentIdx];
        index = parentIdx;
    }
    me->items[index] = value;
}


static void 
__CorePriorityQueue_siftDown(
    CorePriorityQueueRef me, CoreINT_U32 index, const void * value)
{
    CoreINT_U32	half = me->count >> 1;

    while (index < half)
    {
        CorePriorityQueueCallbacks * cb = __CorePriorityQueue_getCallbacks(me);
        CoreINT_U32 childIdx = (index << 1) + 1;
        CoreINT_U32 right = childIdx + 1;
        void * child = me->items[childIdx];
        CoreComparison cmp;

        cmp = cb->compare(me->items[childIdx], me->items[right]);
        if ((right < me->count) && (cmp == CORE_COMPARISON_GREATER_THAN))
        {
            childIdx = right;
            child = me->items[childIdx];
        }

        cmp = cb->compare(value, child);
        if (cmp != CORE_COMPARISON_GREATER_THAN)
        {
            break;
        }

        me->items[index] = child;
        index = childIdx;
    }
    me->items[index] = value;
}

    

static const void * 
__CorePriorityQueue_removeValueAtIndex(CorePriorityQueueRef me, CoreINT_U32 index)
{
    if (index < me->count)
    {
        CorePriorityQueueCallbacks * cb = __CorePriorityQueue_getCallbacks(me);
        const void * last = me->items[--me->count];

        if (cb->compare(last, me->items[(me->count + 1) / 2] ==
            CORE_COMPARISON_LESS_THAN) || (index == 0))
        {
            __CorePriorityQueue_siftDown(me, index, last);
        }
        else
        {
            __CorePriorityQueue_siftUp(me, index, last);
        }
    }
}

/*
static const void * 
_CorePriorityQueue_removeAtIndex(CorePriorityQueueRef me, CoreINT_U32 index)
{
    void * result = null;

    if (index < me->count)
    {
        me->count--;

        if (index == me->count)
        {
            // last item removal
            if (me->callbacks->release != null)
            {
                me->callback->release(me->items[index]);
            }
            me->items[index] = null;
        }
        else
        {
            CoreINT_U32 idx = me->count;
            const void * moved = me->items[idx];

            me->items[idx] = null;
            __CorePriorityQueue_siftDown(me, index, moved);
            if (me->items[index] == moved)
            {
                __CorePriorityQueue_siftUp(me, index, moved);
                if (me->items[index] != moved)
                {
                    result = moved;
                }
            }
        }
    }

    return (const void *) result;
}
*/

static CorePriorityQueueRef
__CorePriorityQueue_init(
    CoreAllocatorRef allocator,
    CoreINT_U32 capacity,
    const void ** values,
    CoreINT_U32 count,
    const CorePriorityQueueCallbacks * callbacks)
{
    struct __CorePriorityQueue * result = null;
    CoreINT_U32 size = sizeof(struct __CorePriorityQueue);
    CorePriorityQueueCallbacksType cbType;
    CoreBOOL isInline = false;
    CoreINT_U32 maxCapacity = 0;
        
    if (__CorePriorityQueue_callbacksMatchNull(callbacks))
    {
        callbacks = &CoreArrayNullCallbacks;
        cbType = CORE_PRIORITY_QUEUE_NULL_CALLBACKS;
    }
    else if (__CorePriorityQueue_callbacksMatchCore(callbacks))
    {
        callbacks = &CoreArrayCoreCallbacks;
        cbType = CORE_PRIORITY_QUEUE_CORE_CALLBACKS;    
    }
    else
    {
        cbType = CORE_PRIORITY_QUEUE_CUSTOM_CALLBACKS;
        size += sizeof(CorePriorityQueueCallbacks);
    }
    
    if (capacity > 0)
    {
        maxCapacity = __CorePriorityQueue_roundUpCapacity(capacity);
        if (maxCapacity < CORE_PRIORITY_QUEUE_INLINE_LIMIT)
        {
            size += maxCapacity * sizeof(const void *);
            isInline = true;
        }
    } 
    
    result = CoreRuntime_createObject(allocator, CorePriorityQueueID, size);
    if (result != null)
    {
        __CorePriorityQueue_setInline(result, isInline);
        __CorePriorityQueue_setCallbacksType(result, cbType);
        __CorePriorityQueue_setCallbacks(result, callbacks);
        result->count = 0;
        result->maxCapacity = (capacity > 0) 
            ? capacity : CORE_PRIORITY_QUEUE_MAX_CAPACITY;
        
        for (idx = 0; idx < count; idx++)
        {
            (void) CorePriorityQueue_addValue(result, values[idx]);
        }
    }
    
    return result;   
}    



/*****************************************************************************
 *
 * Public methods
 * 
 ****************************************************************************/

/* CORE_PUBLIC */ CorePriorityQueueRef
CorePriorityQueue_create(
    CoreAllocatorRef allocator,
    CoreINT_U32 maxCapacity,
    const CorePriorityQueueCallbacks * callbacks)
{
    
    return __CorePriorityQueue_init(allocator, maxCapacity, null, 0, callbacks);    
}


/* CORE_PUBLIC */ CorePriorityQueueRef
CorePriorityQueue_createCopy(
    CoreAllocatorRef allocator,
    CoreINT_U32 maxCapacity,
    CorePriorityQueueRef queue)
{
    CORE_IS_QUEUE_RET1(queue, null);
        
    return __CorePriorityQueue_init(
        allocator, maxCapacity, queue->items, queue->count, &queue->callbacks
    );
}


/* CORE_PUBLIC */ CoreBOOL
CorePriorityQueue_isEmpty(const CorePriorityQueueRef me)
{
	return (me->count == 0) ? true : false;	
}


/* CORE_PUBLIC */ CoreBOOL
CorePriorityQueue_isFull(const CorePriorityQueueRef me)
{
	return (me->count == me->maxCapacity);	
}


/* CORE_PUBLIC */ CoreINT_U32 
CorePriorityQueue_hashCode(const CorePriorityQueueRef me)
{
    //
    // Now I return only the number of items as a hashCode.
    // This is however not a generally convenient approach:
    // 	it should goes through all its items and count hashCode
    //	as a sum of their hashCode, with the me->count involved.
    //	However that can be very slow...
	// 
	return me->count;
}


/* CORE_PUBLIC */ void 
CorePriorityQueue_clear(CorePriorityQueueRef me)
{
    CorePriorityQueueCallbacks * cb = __CorePriorityQueue_getCallbacks(me);
    
    if (cb->release != __CorePriorityQueue_emptyRelease)
    {
        CoreINT_U32 idx;

        for (idx = 0; idx < me->count; idx++)
        {
            cb->release((void *) me->items[idx];
        }
    }

    me->count = 0;
}


/* CORE_PUBLIC */ CoreINT_U32 
CorePriorityQueue_getCount(const CorePriorityQueueRef me)
{
    return me->count;
}


/* CORE_PUBLIC */ CoreINT_U32 
CorePriorityQueue_getCountOfValue(
    const CorePriorityQueueRef me, const void * value)
{
    CoreINT_U32 result = 0;
    CorePriorityQueueCallbacks * cb = __CorePriorityQueue_getCallbacks(me);
    CoreINT_U32 idx;

    if (cb->compare == __CorePriorityQueue_emptyCompare)
    {
        for (idx = 0; idx < me->count; idx++)
        {
            if (value == me->items[idx])
            {
                result++;
            }
        }
    }
    else
    {
        for (idx = 0; idx < me->count; idx++)
        {
            const void * tmp = me->items[idx];

            if ((value == tmp) 
                || cb->compare(value, tmp) == CORE_COMPARISON_EQUAL)
            {
                result++;
            }
        }
    }

    return result;
}


/* CORE_PUBLIC */ CoreBOOL 
CorePriorityQueue_containsValue(
    const CorePriorityQueueRef me, 
    const void * value
)
{
    return (__CorePriorityQueue_indexOfValue(me, value) != CORE_INDEX_NOT_FOUND);
}


/* CORE_PUBLIC */ const void *
CorePriorityQueue_getMinimum(const CorePriorityQueueRef me)
{
    return (CorePriorityQueue_isEmpty(me)) ? null : me->items[0]; 
}


/* CORE_PUBLIC */ CoreBOOL
CorePriorityQueue_addValue(const CorePriorityQueueRef me, const void * value)
{
    CoreBOOL result = true;

    if (me->count >= me->capacity)
    {
        result = __CorePriorityQueue_expand(me, 1);
    }
    if (result)
    {
        CorePriorityQueueCallbacks * cb = __CorePriorityQueue_getCallbacks(me);
        
        cb->retain(value);
        if (me->count == 0)
        {
            me->items[me->count++] = value;
        }
        else
        {
            __CorePriorityQueue_siftUp(me, ++me->count, value);
        }
    }

    return result;
}


/* CORE_PUBLIC */ const void *
CorePriorityQueue_removeMinimum(const CorePriorityQueueRef me)
{
    void * result = null;

    if (!__CorePriorityQueue_isEmtpy(me))
    {
        CorePriorityQueueCallbacks * cb = __CorePriorityQueue_getCallbacks(me);
        const void * tmp = me->items[--me->count];

        result = me->items[0];
        cb->release(result);
        me->items[me->count] = null;
        if (me->count > 0)
        {
            __CorePriorityQueue_siftDown(me, 0, tmp);
        }
    }

    return (const void *) result;
}
 

/* CORE_PUBLIC */ CoreBOOL
CorePriorityQueue_removeValue(const CorePriorityQueueRef me, const void * value)
{
    CoreBOOL result = false;
    CoreINT_U32 idx;

    idx = __CorePriorityQueue_indexOfValue(me, value);
    if (idx != CORE_INDEX_NOT_FOUND)
    {
        CorePriorityQueueCallbacks * cb = __CorePriorityQueue_getCallbacks(me);
                
        cb->release(result);
        (void) __CorePriorityQueue_removeValueAtIndex(me, idx);
        result = true;
    }

    return result;
}


