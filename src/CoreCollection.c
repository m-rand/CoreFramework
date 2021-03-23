

/*****************************************************************************
 *
 * Includes
 * 
 *****************************************************************************/
 
#include "CoreCollection.h"
#include "CoreRuntime.h"
#include "CoreString.h"
#include <stdlib.h>



/*****************************************************************************
 *
 * Types definitions
 * 
 ****************************************************************************/

typedef enum CoreCollectionType
{
    CORE_COLLECTION_IMMUTABLE   = 1,
    CORE_COLLECTION_MUTABLE     = 2,
} CoreCollectionType;


struct __CoreCollection
{
    CoreRuntimeObject core;
    CoreINT_U32	count;              // current number of entries
    CoreINT_U32	capacity;           // allocated capacity
    CoreINT_U32	threshold;          // max number of entries
    CoreINT_U32 maxThreshold;       // zero when unbounded
    CoreINT_U32 marker;
    void ** values;
    CoreINT_U32 * counts;
    /* callback struct -- if custom */
    /* values and counts here -- if immutable */
};




/*****************************************************************************
 *
 * Macros and constants definitions
 * 
 ****************************************************************************/

#define CORE_COLLECTION_HASH_FUNC "BobJenkinsHash"


const CoreCollectionValueCallbacks CoreCollectionValueCoreCallbacks =
{ 
    Core_retain, 
    Core_release, 
    Core_getCopyOfDescription,
    Core_equal,
    Core_hash    
};

static const CoreCollectionValueCallbacks CoreCollectionValueNullCallbacks =
{ 
    null, 
    null, 
    null,
    null,
    null
};



typedef enum CoreCollectionCallbacksType
{
    CORE_COLLECTION_NULL_CALLBACKS   = 1,
    CORE_COLLECTION_CORE_CALLBACKS   = 2,
    CORE_COLLECTION_CUSTOM_CALLBACKS = 3,
} CoreCollectionCallbacksType;



//
// Flags bits:
//  - key callbacks info is in 0-1 bits
//  - value callbacks info is in 2-3 bits
// 	- 
//
#define COLLECTION_TYPE_START       0
#define COLLECTION_TYPE_LENGTH      2  
#define VALUE_CALLBACKS_START       2
#define VALUE_CALLBACKS_LENGTH      2  


#define CORE_COLLECTION_MAX_THRESHOLD   (1 << 30)

#define EMPTY(me)   ((void *)(me)->marker)

#define IS_EMPTY(me, v) ((((CoreINT_U32) (v)) == (me)->marker) ? true: false)

#define DELETED(me)   ((void *)~((me)->marker))

#define IS_DELETED(me, v) ((((CoreINT_U32) (v)) == ~((me)->marker)) ? true: false)

#define IS_VALID(me, v) (!IS_EMPTY(me, v) && !IS_DELETED(me, v))



#define CORE_IS_COLLECTION(dict) CORE_VALIDATE_OBJECT(dict, CoreCollectionID)
#define CORE_IS_COLLECTION_RET0(dict) \
    do { if(!CORE_IS_COLLECTION(dict)) return ;} while (0)
#define CORE_IS_COLLECTION_RET1(dict, ret) \
    do { if(!CORE_IS_COLLECTION(dict)) return (ret);} while (0)



CORE_INLINE CoreCollectionType
__CoreCollection_getType(CoreImmutableCollectionRef me)
{
    return (CoreCollectionType) CoreBitfield_getValue(
        ((const CoreRuntimeObject *) me)->info,
        COLLECTION_TYPE_START,
        COLLECTION_TYPE_LENGTH
    );
}

CORE_INLINE void
__CoreCollection_setType(CoreImmutableCollectionRef me, CoreCollectionType type)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) me)->info,
        COLLECTION_TYPE_START,
        COLLECTION_TYPE_LENGTH,
        (CoreINT_U32) type
    );
}

CORE_INLINE CoreCollectionCallbacksType
__CoreCollection_getValueCallbacksType(CoreImmutableCollectionRef me)
{
    return (CoreCollectionCallbacksType) CoreBitfield_getValue(
        ((const CoreRuntimeObject *) me)->info,
        VALUE_CALLBACKS_START,
        VALUE_CALLBACKS_LENGTH
    );
}

CORE_INLINE void
__CoreCollection_setValueCallbacksType(
    CoreImmutableCollectionRef me, 
    CoreCollectionCallbacksType type
)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) me)->info,
        VALUE_CALLBACKS_START,
        VALUE_CALLBACKS_LENGTH,
        (CoreINT_U32) type
    );
}

CORE_INLINE CoreBOOL
__CoreCollection_valueCallbacksMatchNull(CoreCollectionValueCallbacks * cb)
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
__CoreCollection_valueCallbacksMatchCore(CoreCollectionValueCallbacks * cb)
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

CORE_INLINE CoreCollectionValueCallbacks *
__CoreCollection_getValueCallbacks(CoreImmutableCollectionRef me)
{
    CoreCollectionValueCallbacks * result = null;
    
    switch (__CoreCollection_getValueCallbacksType(me))
    {
        case CORE_COLLECTION_NULL_CALLBACKS:
            result = &CoreCollectionValueNullCallbacks;
            break;
        case CORE_COLLECTION_CORE_CALLBACKS:
            result = &CoreCollectionValueCoreCallbacks;
            break;
        default:
            result = (CoreCollectionValueCallbacks *)
                ((CoreINT_U8 *) me + sizeof(struct __CoreCollection));
            break;
    }
    
    return result;
}


CORE_INLINE CoreINT_U32
__CoreCollection_getSizeOfType(
    CoreImmutableCollectionRef me,
    CoreCollectionType type)
{
    CoreINT_U32 result = 0;
    
    result += sizeof(struct __CoreCollection);    
    if (__CoreCollection_getValueCallbacksType(me) == 
        CORE_COLLECTION_CUSTOM_CALLBACKS)
    {
        result += sizeof(CoreCollectionValueCallbacks);
    }
    
    return result;
}


//
// Returns next power of two higher than the capacity
// threshold for the specified input capacity. Load factor is 3/4.
CORE_INLINE CoreINT_U32 
__CoreCollection_roundUpThreshold(
    CoreINT_U32 capacity
)
{
	return (3 * capacity / 4);
}

//
// Returns next power of two higher than the capacity
// threshold for the specified input capacity. Load factor is 3/4.
CORE_INLINE CoreINT_U32 
__CoreCollection_roundUpCapacity(
    CoreINT_U32 capacity
)
{
	return (capacity < 4)
		? 4  
		: 4 * (1 << (CoreINT_U32)(CoreBits_mostSignificantBit(
			(capacity - 1) / 3) + 1)
		);
}




/*
 * Paul Hsieh's hash...
 */
   
#undef get16bits
#if (defined(__GNUC__) && defined(__i386__)) || defined(__WATCOMC__) \
  || defined(_MSC_VER) || defined (__BORLANDC__) || defined (__TURBOC__)
#define get16bits(d) (*((const CoreINT_U16 *) (d)))
#endif

#if !defined (get16bits)
#define get16bits(d) ((((CoreINT_U32)(((const CoreINT_U8 *)(d))[1])) << 8)\
                       +(CoreINT_U32)(((const CoreINT_U8 *)(d))[0]) )
#endif

CORE_INLINE CoreINT_U32 PaulHsiehHash(CoreINT_U32 code)
{
    CoreINT_U32 hash = 4;
    CoreINT_U32 tmp;
    union
    {
        CoreINT_U16 u16[2];
        CoreINT_U32 v;
    }
    value;
    
    value.v = code;
    
    hash += value.u16[0];
    tmp = (value.u16[1] << 11) ^ hash;
    hash = (hash << 16) ^ tmp;
    hash += hash >> 11;
    hash ^= hash << 3;
    hash += hash >> 5;
    hash ^= hash << 4;
    hash += hash >> 17;
    hash ^= hash << 25;
    hash += hash >> 6;
    
    return hash;
}

#undef get16bits


/*
 * Bob Jenkins' hash for uint32
 */ 

#define rot(x, k) (((x) << (k)) | ((x) >> (32 - (k))))

CORE_INLINE CoreINT_U32
BobJenkinsHash(
    CoreINT_U32 code
)
{
	CoreINT_U32 a;
    CoreINT_U32 b;
    CoreINT_U32 c;

	// Set up the initial state.
	a = b = c = 0xdeadbeef + (1 << 2) + 13;

	a += code;
    c ^= b; c -= rot(b, 14);
    a ^= c; a -= rot(c, 11);
    b ^= a; b -= rot(a, 25);
    c ^= b; c -= rot(b, 16);
    a ^= c; a -= rot(c,  4);
    b ^= a; b -= rot(a, 14);
    c ^= b; c -= rot(b, 24);

    return c;
}

CORE_INLINE CoreINT_U32
AppleHash(
    CoreINT_U32 code
)
{
	CoreINT_U32 a;
    CoreINT_U32 b;
    CoreINT_U32 c;

    a = 0x4B616E65UL;
    b = 0x4B616E65UL;
    c = 1;
    a += code;

    a -= b; a -= c; a ^= (c >> 13);
    b -= c; b -= a; b ^= (a << 8);
    c -= a; c -= b; c ^= (b >> 13);
    a -= b; a -= c; a ^= (c >> 12);
    b -= c; b -= a; b ^= (a << 16);
    c -= a; c -= b; c ^= (b >> 5);
    a -= b; a -= c; a ^= (c >> 3);
    b -= c; b -= a; b ^= (a << 10);
    c -= a; c -= b; c ^= (b >> 15);

    return c;
}

/*
 * Java's Hashmap's rehash.
 */
CORE_INLINE CoreINT_U32 
JavaHash(
    CoreINT_U32 code
)
{
	CoreINT_U32 hash = code;
	
	hash ^= (hash >> 20) ^ (hash >> 12);
	hash ^= (hash >> 7) ^ (hash >> 4);
	
	return hash;
}


/*
 * http://www.concentric.net/~Ttwang/tech/inthash.htm
 */  
CORE_INLINE CoreINT_U32
WangJenkinsHash(
    CoreINT_U32 code
)
{
    CoreINT_U32 hash = code;
    
    hash = (hash+0x7ed55d16) + (hash << 12);
    hash = (hash^0xc761c23c) ^ (hash >> 19);
    hash = (hash+0x165667b1) + (hash <<  5);
    hash = (hash+0xd3a2646c) ^ (hash <<  9);
    hash = (hash+0xfd7046c5) + (hash <<  3);
    hash = (hash^0xb55a4f09) ^ (hash >> 16);
    
    return hash;
}

/*
 * http://www.concentric.net/~Ttwang/tech/inthash.htm
 */  
CORE_INLINE CoreINT_U32
WangJenkinsHash2(
    CoreINT_U32 code
)
{
    CoreINT_U32 hash = code;
    
    hash = ~hash + (hash << 15); // hash = (hash << 15) - hash - 1;
    hash =  hash ^ (hash >> 12);
    hash =  hash + (hash <<  2);
    hash =  hash ^ (hash >>  4);
    hash =  hash * 2057; // hash = (hash + (hash << 3)) + (hash << 11);
    hash =  hash ^ (hash >> 16);

    return hash;
}

/*
 * http://www.concentric.net/~Ttwang/tech/inthash.htm
 */  
CORE_INLINE CoreINT_U32
WangJenkinsHash3(
    CoreINT_U32 code
)
{
    CoreINT_U32 hash = code;
    CoreINT_U32 c2 = 0x27d4eb2d; // a prime or an odd constant

    hash = (hash ^ 61) ^ (hash >> 16);
    hash = hash + (hash << 3);
    hash = hash ^ (hash >> 4);
    hash = hash * c2;
    hash = hash ^ (hash >> 15);
    
    return hash;
}


CORE_INLINE CoreHashCode
__CoreCollection_rehashValue(
    CoreHashCode code
)
{
    return BobJenkinsHash(code);
}


CORE_INLINE CoreINT_U32
__CoreCollection_getIndexForHashCode(
    CoreImmutableCollectionRef me,
    CoreHashCode hashCode)
{
    return (hashCode & (me->capacity - 1));
}


static CoreINT_U32
__CoreCollection_getBucketForValue_1(
    CoreImmutableCollectionRef me,
    const void * value
)
{
    CoreINT_U32 result      = CORE_INDEX_NOT_FOUND;
    const void ** values    = me->values;
    CoreINT_U32 valueHash   = 0;
    CoreINT_U32 probe       = 0;
    CoreINT_U32 start       = 0;
       
    valueHash = __CoreCollection_rehashValue((CoreHashCode) value);
    probe = __CoreCollection_getIndexForHashCode(me, valueHash);
    start = probe;

    for ( ; !IS_EMPTY(me, values[probe]) && (probe < me->capacity); probe++)
    {
        if (!IS_DELETED(me, values[probe]))
        {
            if (values[probe] == value)
            {
                result = probe;
                break;
            }
        }
    }
    
    if (result == CORE_INDEX_NOT_FOUND)
    {
        for (probe = 0; !IS_EMPTY(me, values[probe]) && (probe < start); probe++)
        {
            if (!IS_DELETED(me, values[probe]))
            {
                if (values[probe] == value)
                {
                    result = probe;
                    break;
                }
            }
        }
    }

    return result;
}


static CoreINT_U32
__CoreCollection_getBucketForValue_2(
    CoreImmutableCollectionRef me,
    const void * value,
    CoreCollectionValueCallbacks * cb
)
{
    CoreINT_U32 result              = CORE_INDEX_NOT_FOUND;
    CoreINT_U32 valueHash           = 0;
    CoreINT_U32 probe               = 0;
    CoreINT_U32 start               = 0;
    const void ** values            = me->values;
    CoreBOOL (* opEqual)(CoreObjectRef, CoreObjectRef);
        
    valueHash = __CoreCollection_rehashValue(cb->hash(value));
    probe = __CoreCollection_getIndexForHashCode(me, valueHash);
    opEqual = cb->equal;
    start = probe;
    
    for ( ; !IS_EMPTY(me, values[probe]) && (probe < me->capacity); probe++)
    {
        if (!IS_DELETED(me, values[probe]))
        {
            if ((values[probe] == value) || opEqual(value, values[probe]))
            {
                result = probe;
                break;
            }
        }
    }
    
    if (result == CORE_INDEX_NOT_FOUND)
    {
        for (probe = 0; !IS_EMPTY(me, values[probe]) && (probe < start); probe++)
        {
            if (!IS_DELETED(me, values[probe]))
            {
                if ((values[probe] == value) || opEqual(value, values[probe]))
                {
                    result = probe;
                    break;
                }
            }
        }
    }

    return result;
}

// Warning! Shouldn't be called when count = 0 (storage may not allocated yet)!
CORE_INLINE CoreINT_U32
__CoreCollection_getBucketForValue(
    CoreImmutableCollectionRef me,
    const void * value
)
{
    CoreINT_U32 result;
    CoreCollectionCallbacksType cbType;
    
    cbType = __CoreCollection_getValueCallbacksType(me);
    if (cbType == CORE_COLLECTION_NULL_CALLBACKS)
    {
        result = __CoreCollection_getBucketForValue_1(me, value);
    }
    else
    {
        CoreCollectionValueCallbacks * cb;
        
        cb = __CoreCollection_getValueCallbacks(me);
        if (cb->equal == null)
        {
            result = __CoreCollection_getBucketForValue_1(me, value);
        }
        else
        {
            result = __CoreCollection_getBucketForValue_2(me, value, cb);        
        }
    }
        
    return result;
}


static void
__CoreCollection_findBuckets_1(
    CoreImmutableCollectionRef me,
    const void * value,
    CoreINT_U32 * match,
    CoreINT_U32 * empty
)
{
    CoreINT_U32 valueHash   = 0;
    CoreINT_U32 probe       = 0;
    CoreINT_U32 start       = 0;
    const void ** values    = me->values;      
       
    *match = CORE_INDEX_NOT_FOUND;
    *empty = CORE_INDEX_NOT_FOUND;    
    valueHash = __CoreCollection_rehashValue((CoreHashCode) value);
    probe = __CoreCollection_getIndexForHashCode(me, valueHash);
    start = probe;

    // really hard to keep Misra-C instructions...
    for ( ; probe < me->capacity; probe++)
    {
        if (IS_EMPTY(me, values[probe]))
        {
            if (*empty == CORE_INDEX_NOT_FOUND)
            {
                *empty = probe;
            }
            break;                 
        }
        else if (IS_DELETED(me, values[probe]))
        {
            if (*empty == CORE_INDEX_NOT_FOUND)
            {
                *empty = probe;
            }                
        }
        else
        {
            if (value == values[probe])
            {
                *match = probe;
                break;            
            }
        }
    }
    
    if ((*empty == CORE_INDEX_NOT_FOUND) && (*match == CORE_INDEX_NOT_FOUND))
    {
        for (probe = 0; probe < start; probe++)
        {
            if (IS_EMPTY(me, values[probe]))
            {
                if (*empty == CORE_INDEX_NOT_FOUND)
                {
                    *empty = probe;
                }
                break;                 
            }
            else if (IS_DELETED(me, values[probe]))
            {
                if (*empty == CORE_INDEX_NOT_FOUND)
                {
                    *empty = probe;
                }                
            }
            else
            {
                if (value == values[probe])
                {
                    *match = probe;
                    break;            
                }
            }
        }
    }
}


static void
__CoreCollection_findBuckets_2(
    CoreImmutableCollectionRef me,
    const void * value,
    CoreCollectionValueCallbacks * cb,
    CoreINT_U32 * match,
    CoreINT_U32 * empty
)
{
    CoreINT_U32 valueHash   = 0;
    CoreINT_U32 probe       = 0;
    CoreINT_U32 start       = 0;
    const void ** values    = me->values;    
    CoreBOOL (* opEqual)(CoreObjectRef, CoreObjectRef);
               
    *match = CORE_INDEX_NOT_FOUND;
    *empty = CORE_INDEX_NOT_FOUND;
    opEqual = cb->equal;        
    valueHash = __CoreCollection_rehashValue(cb->hash(value));
    probe = __CoreCollection_getIndexForHashCode(me, valueHash);
    start = probe;

    // really hard to keep Misra-C instructions...
    for ( ; probe < me->capacity; probe++)
    {
        if (IS_EMPTY(me, values[probe]))
        {
            if (*empty == CORE_INDEX_NOT_FOUND)
            {
                *empty = probe;
            }
            break;                 
        }
        else if (IS_DELETED(me, values[probe]))
        {
            if (*empty == CORE_INDEX_NOT_FOUND)
            {
                *empty = probe;
            }                
        }
        else
        {
            if (opEqual(value, values[probe]))
            {
                *match = probe;
                break;            
            }
        }
    }
    
    if ((*empty == CORE_INDEX_NOT_FOUND) && (*match == CORE_INDEX_NOT_FOUND))
    {
        for (probe = 0; probe < start; probe++)
        {
            if (IS_EMPTY(me, values[probe]))
            {
                if (*empty == CORE_INDEX_NOT_FOUND)
                {
                    *empty = probe;
                }
                break;                 
            }
            else if (IS_DELETED(me, values[probe]))
            {
                if (*empty == CORE_INDEX_NOT_FOUND)
                {
                    *empty = probe;
                }                
            }
            else
            {
                if (opEqual(value, values[probe]))
                {
                    *match = probe;
                    break;            
                }
            }
        }
    }
}

// Warning! Shouldn't be called when count = 0 (storage may not allocated yet)!
static void
__CoreCollection_findBuckets(
    CoreImmutableCollectionRef me,
    const void * value,
    CoreINT_U32 * match,
    CoreINT_U32 * empty
)
{
    CoreCollectionCallbacksType cbType;

    cbType = __CoreCollection_getValueCallbacksType(me);
    if (cbType == CORE_COLLECTION_NULL_CALLBACKS)
    {
        __CoreCollection_findBuckets_1(me, value, match, empty);
    }
    else
    {
        CoreCollectionValueCallbacks * cb;

        cb = __CoreCollection_getValueCallbacks(me);
        if (cb->equal == null)
        {
            __CoreCollection_findBuckets_1(me, value, match, empty);
        }
        else
        {
            __CoreCollection_findBuckets_2(me, value, cb, match, empty);
        }
    }
}


CORE_INLINE CoreBOOL
__CoreCollection_isValueMagic(
    CoreImmutableCollectionRef me,
    const void * value
)
{
    return (IS_EMPTY(me, value) || IS_DELETED(me, value)) ? true: false;
}


static void
__CoreCollection_changeMarker(    
    CoreImmutableCollectionRef me
)
{
    CoreBOOL hit;
	CoreINT_U32 idx;
    CoreINT_U32 newMarker = me->marker;
	CoreINT_U32 n = me->capacity;
	const void ** values = me->values;
    
	// Find the new empty.
    do
	{
        newMarker--;
		hit = false;
		for (idx = 0; idx < n; idx++)
		{
			if ((newMarker == (CoreINT_U32) values[idx]) ||
			    (~newMarker == (CoreINT_U32) values[idx]))             
			{
				hit = true;
				break;
			}
		}
	}
	while (hit);
	
    ((struct __CoreCollection *) me)->marker = newMarker;
    
	// Update the table with new empty.
    for (idx = 0; idx < n; idx++)
	{
		if (me->marker == (CoreINT_U32) values[idx])
		{
			values[idx] = (void *) EMPTY(me);
		}
	}
}


CORE_INLINE void
__CoreCollection_transfer(
    CoreCollectionRef me, 
    const void ** oldValues, 
    const void ** oldCounts,
    CoreINT_U32 oldCapacity)
{
    CoreINT_U32 idx;
    
    for (idx = 0; idx < oldCapacity; idx++)
    {
        const void * tmpValue = oldValues[idx];
        
        if (IS_VALID(me, tmpValue))
        {
            CoreINT_U32 match;
            CoreINT_U32 empty;
            
            __CoreCollection_findBuckets(me, tmpValue, &match, &empty);
            if (empty != CORE_INDEX_NOT_FOUND)
            {
                me->values[empty] = tmpValue;
                me->counts[empty] = oldCounts[idx];
            }
        }
    }
}


static CoreBOOL
__CoreCollection_expand(CoreCollectionRef me, CoreINT_U32 needed)
{
    CoreBOOL result = false;
    CoreINT_U32 neededCapacity = me->count + needed;
    
    if (neededCapacity <= me->maxThreshold)
    {
        const void ** oldValues = me->values;
        void ** newValues = null;
        CoreINT_U32 * newCounts = null;
        CoreINT_U32 oldCapacity = me->capacity;
        CoreAllocatorRef allocator = null;
        
        me->capacity = __CoreCollection_roundUpCapacity(neededCapacity); 
        me->threshold = min(
            __CoreCollection_roundUpThreshold(me->capacity),
            me->maxThreshold
        );
        allocator = Core_getAllocator(me);
        newValues = CoreAllocator_allocate(
            allocator,
            me->capacity * sizeof(void *)
        );
        newCounts = CoreAllocator_allocate(
            allocator,
            me->capacity * sizeof(CoreINT_U32)
        );
        
        if ((newValues != null) && (newCounts != null))
        {
            // Reset the whole table of values.
            CoreINT_U32 idx;
            
            me->values = newValues;
            me->counts = newCounts;
            for (idx = 0; idx < me->capacity; idx++)
            {
                me->values[idx] = EMPTY(me);
            }
            
            // Now transfer content of old table to the new one.
            if (oldValues != null)
            {
                __CoreCollection_transfer(me, oldValues, oldCounts, oldCapacity);            
                CoreAllocator_deallocate(allocator, oldValues);
                CoreAllocator_deallocate(allocator, oldCount);
            }
            result = true;
        }
    }
    
    return result; 				
}


CORE_INLINE CoreBOOL
__CoreCollection_shouldShrink(CoreCollectionRef me)
{
    return false;
}


CORE_INLINE void
__CoreCollection_shrink(CoreCollectionRef me)
{
    return ;
}


CORE_INLINE CoreBOOL
__CoreCollection_addValue(
    CoreCollectionRef me, 
    const void * value)
{
    CoreBOOL result  = false;
    CoreBOOL ready   = true;
    
    if ((me->values == null) || (me->count >= me->threshold))
    {
        ready = __CoreCollection_expand(me, 1);
    }
        
    if (ready)
    {
        CoreINT_U32 match;
        CoreINT_U32 empty;
                 
        // Check the value for magic value.
        if (__CoreCollection_isValueMagic(me, value))
        {
            __CoreCollection_changeMarker(me);
        }
        
        __CoreCollection_findBuckets(me, value, &match, &empty);
        if (match == CORE_INDEX_NOT_FOUND)
        {
            CoreCollectionValueCallbacks * valueCb;
        
            valueCb = __CoreCollection_getValueCallbacks(me);
            if (valueCb->retain != null)
            {
                valueCb->retain(value);
            }
            me->values[empty] = value;
            me->counts[empty] = 1;
        }
        else
        {
            me->counts[match]++;
        }
        me->count++;
        result = true;
    }
    
    return result;
}


CORE_INLINE CoreBOOL
__CoreCollection_removeValue(
    CoreCollectionRef me, 
    const void * value)
{
	CoreBOOL result = false;
    CoreINT_U32 index = __CoreCollection_getBucketForValue(me, value);
        
    if (index != CORE_INDEX_NOT_FOUND)
    {
        if (me->counts[index] > 1)
        {
            me->counts[index]--;
            me->count--;
        }
        else
        {
            CoreCollectionValueCallbacks * valueCb;
    
            valueCb = __CoreCollection_getValueCallbacks(me);
            if (valueCb->release != null)
            {
                valueCb->release(me->values[index]);
            }
            
            me->count--;
            me->values[index] = DELETED(me);
            me->counts[index] = 0;
                   
            if (__CoreCollection_shouldShrink(me))
            {
                __CoreCollection_shrink(me);
            }
            else
            {
                // All deleted slots followed by an empty slot will be converted
                // to an empty slot.
                if ((index < me->capacity) && (IS_EMPTY(me, me->values[index + 1])))
                {
                    CoreINT_S32 idx = (CoreINT_S32) index;
                    for ( ; (idx >= 0) && IS_DELETED(me, me->values[idx]); idx--)
                    {
                        me->values[idx] = EMPTY(me);
                    }
                }
            }
        }
        result = true;        
    }
    
    return result;
}


CORE_INLINE CoreBOOL
__CoreCollection_replaceValue(
    CoreCollectionRef me, 
    const void * value)
{
	CoreBOOL result = false;
    CoreINT_U32 index = __CoreCollection_getBucketForValue(me, value);
        
    if (index != CORE_INDEX_NOT_FOUND)
    {
        CoreCollectionValueCallbacks * valueCb;

        valueCb = __CoreCollection_getValueCallbacks(me);
        if (valueCb->release != null)
        {
            valueCb->release(me->values[index]);
        }
        if (valueCb->retain != null)
        {
            valueCb->retain(value);
        }
        result = true;
    }
    
    return result;
}


CORE_INLINE void 
__CoreCollection_clear(CoreCollectionRef me)
{
    CoreCollectionValueCallbacks * valueCb;
    
    valueCb = __CoreCollection_getValueCallbacks(me);
    if ((me->values != null) && (valueCb->release != null))
    {
        CoreINT_U32 idx;
        
        for (idx = 0; idx < me->capacity; idx++)
        {
            void * value = me->values[idx];
            if (IS_VALID(me, value))
            {
                if (valueCb->release != null)
                {
                    valueCb->release(value);
                    me->counts[idx] = 0;
                }
            }
        }
    }
}


static void
__CoreCollection_cleanup(CoreObjectRef me)
{
    struct __CoreCollection * _me = (struct __CoreCollection *) me;
        
    __CoreCollection_clear(_me);
    switch(__CoreCollection_getType(_me))
    {
        case CORE_COLLECTION_MUTABLE:
        {
            if (_me->values != null)
            {
                CoreAllocatorRef allocator = Core_getAllocator(me);
                CoreAllocator_deallocate(allocator, _me->values);
            }
            break;
        }
    }
}


static CoreBOOL
__CoreCollection_equal(CoreObjectRef me, CoreObjectRef to)
{
    return false;
}


static CoreHashCode
__CoreCollection_hash(CoreObjectRef me)
{
    return ((CoreImmutableCollectionRef) me)->count;
}


static char *
__CoreCollection_getCopyOfDescription(CoreObjectRef me)
{
    return null;
}




#ifdef __WIN32__
#define _strcat(dst, src, cnt) strcat_s(dst, cnt, src)
#else
#define _strcat(dst, src, cnt) strcat(dst, src)
#endif

static void
__CoreCollection_collisionsForValue(
    CoreImmutableCollectionRef me,
    const void * value,
    CoreINT_U32 * collisions,
    CoreINT_U32 * comparisons
)
{
    CoreINT_U32 valueHash     = 0;
    CoreINT_U32 idx         = 0;
    CoreINT_U32 start       = 0;
    void *      cmpValue      = null;
    const void * realValue    = null;
	CoreCollectionValueCallbacks * cb = __CoreCollection_getValueCallbacks(me);       
    
    valueHash = __CoreCollection_rehashValue((cb->hash) ? cb->hash(value) : (CoreHashCode) value);
    idx = __CoreCollection_getIndexForHashCode(me, valueHash);
    start = idx;
    *collisions = 0;
    *comparisons = 0;    
    
    do 
    {
        CoreINT_U32 cmpHash = 0;
        CoreINT_U32 cmpIdx = 0;
        cmpValue = me->values[idx];
        
        cmpHash = __CoreCollection_rehashValue((cb->hash) ? cb->hash(cmpValue) : (CoreHashCode) cmpValue);
        cmpIdx = __CoreCollection_getIndexForHashCode(me, cmpHash);
        
        *comparisons += 1;
        if ((cmpValue == realValue) || (cb->equal) ? (realValue, cmpValue) : false)
        {
            break;            
        }        
        if (cmpIdx == start)
        {
            *collisions += 1;
        }

        idx++;
        if (idx >= me->capacity)
        {
            idx -= me->capacity;
        }
    }
    while (!IS_EMPTY(me, cmpValue) && (idx != start));
}

/* CORE_PROTECTED */ char * 
_CoreCollection_description(CoreImmutableCollectionRef me)
{
	CoreINT_U32 size = 200 + me->capacity * 80;
    char * result = (char *) malloc(size);
	
	if (result != null)
	{
		CoreINT_U32 nullCount = 0;
		CoreCHAR_8 s[150];
		CoreINT_U32 idx;
		CoreINT_U32 maxCollisions = 0;
		CoreINT_U32 maxComparisons = 0;
		CoreINT_U32 nullBuckets = 0;
		CoreCollectionValueCallbacks * cb = __CoreCollection_getValueCallbacks(me);
		
		sprintf(s, "CoreCollection <%p>\n{\n  capacity = %u, count = %u, \
threshold = %u\n  Buckets:\n", me, me->capacity, me->count, me->threshold);
		strcat(result, s);
            
        for (idx = 0; (me->values != null) && (idx < me->capacity); idx++)
        {
            CoreINT_U32 collisions = 0;
            CoreINT_U32 comparisons = 0;
            CoreINT_U32 hashCode = 0;
            CoreCHAR_8 s2[150];
            
            if (!IS_EMPTY(me, me->values[idx]))
            {
                __CoreCollection_collisionsForValue(
                    me, me->values[idx], &collisions, &comparisons
                );
                hashCode = __CoreCollection_rehashValue(
                    (cb->hash) ? cb->hash(me->values[idx]) : (CoreHashCode) me->values[idx]
                );
#if 0
                sprintf(
                    s2, 
					"  [%5d] - value[%p], hash 0x%08x, idx[%5d], "
                    "coll %u, comp %u\n", 
                    idx, 
                    me->values[idx], 
                    hashCode,
					__CoreCollection_getIndexForHashCode(me, hashCode),
                    collisions,
                    comparisons
                );
                strcat(result, s2);
#endif
                maxCollisions = max(maxCollisions, collisions);
                maxComparisons = max(maxComparisons, comparisons);
           }
            else
            {
#if 0
                sprintf(s2, "  [%5d] - empty\n", 
                    idx 
                );
                strcat(result, s2);
#endif
                nullBuckets++;
            }
        }

        sprintf(
			s, 
			"  Internal info:\n  - max collisions %u\n"
            "  - max comparisons %u\n  - null buckets %u\n", 
			maxCollisions, maxComparisons, nullBuckets
		);
        strcat(result, s);
        sprintf(
            s,
            "  - used memory: %u B\n  - used rehash: %s\n}\n",
            sizeof(struct __CoreCollection) + 
            (me->capacity * 2) * sizeof(void *),
            CORE_COLLECTION_HASH_FUNC
        );
        strcat(result, s);
    }
	
	return result;
}






static CoreClassID CoreCollectionID = CORE_CLASS_ID_UNKNOWN;

static const CoreClass __CoreCollectionClass =
{
    0x00,                            // version
    "CoreCollection",                       // name
    NULL,                            // init
    NULL,                            // copy
    __CoreCollection_cleanup,               // cleanup
    __CoreCollection_equal,                 // equal
    __CoreCollection_hash,                  // hash
    __CoreCollection_getCopyOfDescription   // getCopyOfDescription
};


/* CORE_PROTECTED */ void
CoreCollection_initialize(void)
{
    CoreCollectionID = CoreRuntime_registerClass(&__CoreCollectionClass);
}



static struct __CoreCollection *
__CoreCollection_init(
    CoreAllocatorRef allocator,
    CoreINT_U32 capacity,
    const CoreCollectionValueCallbacks * valueCallbacks,
    CoreBOOL isMutable
)
{
    struct __CoreCollection * result = null;
    CoreINT_U32 size = sizeof(struct __CoreCollection);
    CoreCollectionType type;
    CoreCollectionCallbacksType valueCbType;
    
    if (isMutable)
    {
        type = CORE_COLLECTION_MUTABLE;
    }
    else
    {
        type = CORE_COLLECTION_IMMUTABLE;
        capacity = __CoreCollection_roundUpCapacity(capacity);
        size += capacity * sizeof(const void *);
        size += capacity * sizeof(CoreINT_U32);
    }

    if (__CoreCollection_valueCallbacksMatchNull(valueCallbacks))
    {
        valueCallbacks = &CoreCollectionValueNullCallbacks;
        valueCbType = CORE_COLLECTION_NULL_CALLBACKS;
    }
    else if (__CoreCollection_valueCallbacksMatchCore(valueCallbacks))
    {
        valueCallbacks = &CoreCollectionValueCoreCallbacks;
        valueCbType = CORE_COLLECTION_CORE_CALLBACKS;    
    }
    else
    {
        valueCbType = CORE_COLLECTION_CUSTOM_CALLBACKS;
        size += sizeof(CoreCollectionValueCallbacks);
    }
        
    result = CoreRuntime_createObject(allocator, CoreCollectionID, size);
    if (result != null)
    {
        __CoreCollection_setType(result, type);
        __CoreCollection_setValueCallbacksType(result, valueCbType);
        result->maxThreshold = (capacity == 0) 
            ? CORE_COLLECTION_MAX_THRESHOLD 
            : min(capacity, CORE_COLLECTION_MAX_THRESHOLD);
        result->count = 0;
        result->capacity = 0;
        result->marker = 0xdeadbeef;
        result->values = null;
        
        if (valueCbType == CORE_COLLECTION_CUSTOM_CALLBACKS)
        {
            CoreCollectionValueCallbacks * valueCb;
            valueCb = __CoreCollection_getValueCallbacks(result);
            *valueCb = *valueCallbacks;                
        }
        
        switch (type)
        {
            case CORE_COLLECTION_IMMUTABLE:
            {
                CoreINT_U32 size;
                
                result->threshold = result->maxThreshold;
                size = __CoreCollection_getSizeOfType(result, type);
                result->values = (void *) ((CoreINT_U8 *) result + 
                    sizeof(struct __CoreCollection) + 
                    size);
                result->counts = (void *) ((CoreINT_U8 *) result +
                    sizeof(struct __CoreDictionary) + 
                    size + 
                    capacity * sizeof(const void *));
 
                break;
            }
        }
    }
    
    return result;   
}


/* CORE_PUBLIC */ CoreCollectionRef
CoreCollection_create(
    CoreAllocatorRef allocator,
    CoreINT_U32 capacity,
    const CoreCollectionValueCallbacks * valueCallbacks
)
{
    return __CoreCollection_init(
        allocator,
        capacity,
        valueCallbacks,
        true
    );
}


/* CORE_PUBLIC */ CoreCollectionRef
CoreCollection_createImmutable(
    CoreAllocatorRef allocator,
    const void ** values,
    CoreINT_U32 count,
    const CoreCollectionValueCallbacks * valueCallbacks
)
{
    CoreCollectionRef result = null;
    
    CORE_ASSERT_RET1(
        null,
        (values != null) || (count == 0),
        CORE_LOG_ASSERT,
        "%s(): values cannot be NULL when count %u is > 0",
        __PRETTY_FUNCTION__
    );
    
    result = __CoreCollection_init(
        allocator,
        count,
        valueCallbacks,
        false
    );
    if (result != null)
    {
        // temporarily switch to mutable variant and add all the keys-values
        CoreINT_U32 idx;
        
        __CoreCollection_setType(result, CORE_COLLECTION_MUTABLE);
        for (idx = 0; idx < count; idx++)
        {
            CoreCollection_addValue(result, values[idx]);
        }
        __CoreCollection_setType(result, CORE_COLLECTION_IMMUTABLE);
    }
    
    return result;
}


/* CORE_PUBLIC */ CoreCollectionRef
CoreCollection_createCopy(
    CoreAllocatorRef allocator,
    CoreImmutableCollectionRef col,
    CoreINT_U32 capacity
)
{
    CoreCollectionRef result;
    const CoreCollectionValueCallbacks * valueCallbacks;
    CoreINT_U32 count;

    CORE_IS_COLLECTION_RET1(col, null);
    
    count = CoreCollection_getCount(col);
    valueCallbacks = __CoreCollection_getValueCallbacks(col);
    
    result = __CoreCollection_init(
        allocator, 
        capacity, 
        valueCallbacks,
        true
    );
    if ((result != null) && (count > 0))
    {
        CoreINT_U32 idx;
        const void * valuebuf[64];
        const void ** values;
        
        values = (count <= 64) ? valuebuf : CoreAllocator_allocate(
            allocator,
            count * sizeof(const void *)
        );
        
        CoreCollection_copyValues(col, values);
        if (capacity == 0)
        {
            __CoreCollection_expand(result, count);
        }
        
        for (idx = 0; idx < count; idx++)
        {
            CoreCollection_addValue(result, values[idx]);
        }       

        if (values != valuebuf)
        {
            CoreAllocator_deallocate(allocator, values);
        }
    }
    
    return result;    
}


/* CORE_PUBLIC */ CoreImmutableCollectionRef
CoreCollection_createImmutableCopy(
    CoreAllocatorRef allocator,
    CoreImmutableCollectionRef col
)
{
    CoreCollectionRef result;
    const CoreCollectionValueCallbacks * valueCallbacks;
    CoreINT_U32 count;

    CORE_IS_COLLECTION_RET1(col, null);
    
    count = CoreCollection_getCount(col);
    valueCallbacks = __CoreCollection_getValueCallbacks(col);

    result = __CoreCollection_init(
        allocator, 
        count, 
        valueCallbacks,
        false
    );
    if ((result != null) && (count > 0))
    {
        CoreINT_U32 idx;
        const void * valuebuf[64];
        const void ** values;
        
        values = (count <= 64) ? valuebuf : CoreAllocator_allocate(
            allocator,
            count * sizeof(const void *)
        );
        
        CoreCollection_copyValues(col, values);

        __CoreCollection_setType(result, CORE_COLLECTION_MUTABLE);        
        for (idx = 0; idx < count; idx++)
        {
            CoreCollection_addValue(result, values[idx]);
        }       
        __CoreCollection_setType(result, CORE_COLLECTION_IMMUTABLE);
        
        if (values != valuebuf)
        {
            CoreAllocator_deallocate(allocator, values);
        }
    }
    
    return (CoreImmutableCollectionRef) result;    
}


/* CORE_PUBLIC */ CoreINT_U32
CoreCollection_getCount(CoreImmutableCollectionRef me)
{
    CORE_IS_COLLECTION_RET1(me, 0);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    
    return me->count;
}


/* CORE_PUBLIC */ CoreINT_U32
CoreCollection_getCountOfValue(
    CoreImmutableCollectionRef me, 
    const void * value
)
{
    CoreINT_U32 result = 0;
    CoreINT_U32 index;
    
    CORE_IS_COLLECTION_RET1(me, 0);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    
    index = __CoreCollection_getBucketForValue(me, value);
    if (index != CORE_INDEX_NOT_FOUND)
    {
        result = me->counts[index];
    }
    
    return result;
}


/* CORE_PUBLIC */ const void *
CoreCollection_getValue(CoreImmutableCollectionRef me, const void * value)
{
    void * result = null;
    CoreINT_U32 idx;

    CORE_IS_COLLECTION_RET1(me, null);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
      
    if (me->count > 0)
    {
        idx = __CoreCollection_getBucketForValue(me, value);         
        if (idx != CORE_INDEX_NOT_FOUND)
        {
            result = me->values[idx];
        }
    }
    
    return result;
}


/* CORE_PUBLIC */ CoreBOOL
CoreCollection_getValueIfPresent(
    CoreImmutableCollectionRef me, 
    const void * candidate,
    void ** value
)
{
    CoreBOOL result = false;
    CoreINT_U32 idx;

    CORE_IS_COLLECTION_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);

    if ((value != null) && (me->count > 0))
    {
        idx = __CoreCollection_getBucketForValue(me, candidate);
        if (idx != CORE_INDEX_NOT_FOUND)
        {
            *value = me->values[indx];
            result = true;
        }
    }
    
    return result;    
}


/* CORE_PUBLIC */ CoreBOOL
CoreCollection_containsValue(CoreImmutableCollectionRef me, const void * value)
{
    CoreBOOL result = false;
    CoreINT_U32 idx;

    CORE_IS_COLLECTION_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);

    if (me->count > 0)
    {
        idx = __CoreCollection_getBucketForValue(me, value);         
        if (idx != CORE_INDEX_NOT_FOUND)
        {
            result = true;
        }
    }
        
    return result;
}
   

/* CORE_PUBLIC */ void
CoreCollection_copyValues(
    CoreImmutableCollectionRef me,
    void ** values
)
{
    CORE_IS_COLLECTION_RET0(me);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET0(
        (values != null),
        CORE_LOG_ASSERT,
        "%s(): values %p is null!",
        __PRETTY_FUNCTION__
    );
    
    if (me->count > 0)
    {
        const void ** _values;
        CoreINT_U32 idx;
        CoreINT_U32 n;

        _values = me->values;
        for (idx = 0, n = me->capacity; idx < n; idx++)
        {
            if (IS_VALID(me, _values[idx]))
            {
                *values++ = _values[idx];             
            }
        }
    }        
}


/* CORE_PROTECTED */ CoreINT_U32
_CoreCollection_iterate(
    CoreImmutableCollectionRef me, 
    __CoreIteratorState * state,
    void * buffer,
    CoreINT_U32 count
)
{
    CoreINT_U32 result = 0;
    
    state->items = buffer;
    if (me->count > 0)
    {
        const void ** values;
        CoreINT_U32 idx = state->state;
        CoreINT_U32 n = me->capacity;

        values = me->values;
        for ( ; (idx < n) && (result < count); idx++)
        {
            if (IS_VALID(me, values[idx]))
            {
                state->items[result++] = values[idx];             
            }
            state->state++;
        }
    } 
    
    return result;
}       
    
    


/* CORE_PUBLIC */ CoreBOOL
CoreCollection_addValue(
    CoreCollectionRef me, 
    const void * value)
{
    CORE_IS_COLLECTION_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreCollection_getType(me) != CORE_COLLECTION_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );

    return __CoreCollection_addValue(me, value);
}


/* CORE_PUBLIC */ CoreBOOL
CoreCollection_removeValue(
    CoreCollectionRef me, 
    const void * value)
{
    CORE_IS_COLLECTION_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreCollection_getType(me) != CORE_COLLECTION_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    
    return __CoreCollection_removeValue(me, value);
}


/* CORE_PUBLIC */ CoreBOOL
CoreCollection_replaceValue(
    CoreCollectionRef me, 
    const void * value)
{
    CORE_IS_COLLECTION_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreCollection_getType(me) != CORE_COLLECTION_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    
    return __CoreCollection_replaceValue(me, value);
}


/* CORE_PUBLIC */ void
CoreCollection_clear(CoreCollectionRef me)
{
    CORE_IS_COLLECTION_RET0(me);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET0(
        (__CoreCollection_getType(me) != CORE_COLLECTION_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );

    __CoreCollection_clear(me);
} 


/* CORE_PUBLIC */ void
CoreCollection_applyFunction(
    CoreImmutableCollectionRef me,
    CoreCollectionApplyFunction map,
    void * context    
)
{
    CoreINT_U32 idx, n;
    
    CORE_IS_COLLECTION_RET0(me);    
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET0(
        map != null,
        CORE_LOG_ASSERT,
        "%s(): apply function cannot be null!",
        __PRETTY_FUNCTION__
    );

    for (idx = 0, n = me->capacity; idx < n; idx++)
    {
        const void * item = me->values[idx];
        if (IS_VALID(me, item))
        {
            map(item, context);
        }
    }
}
