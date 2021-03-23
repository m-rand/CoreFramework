

/*****************************************************************************
 *
 * Includes
 * 
 *****************************************************************************/
 
#include "CoreSet.h"
#include "CoreRuntime.h"
#include "CoreString.h"
#include <stdlib.h>



/*****************************************************************************
 *
 * Types definitions
 * 
 ****************************************************************************/

typedef enum CoreSetType
{
    CORE_SET_IMMUTABLE   = 1,
    CORE_SET_MUTABLE     = 2,
} CoreSetType;


struct __CoreSet
{
    CoreRuntimeObject core;
    CoreINT_U32	count;              // current number of entries
    CoreINT_U32	capacity;           // allocated capacity
    CoreINT_U32	threshold;          // max number of entries
    CoreINT_U32 maxThreshold;       // zero when unbounded
    CoreINT_U32 marker;
    const void ** values;
    /* value callback struct -- if custom */
    /* values here -- if immutable */    
};




/*****************************************************************************
 *
 * Macros and constants definitions
 * 
 ****************************************************************************/

#define CORE_SET_HASH_FUNC "BobJenkinsHash"


const CoreSetValueCallbacks CoreSetValueCoreCallbacks =
{ 
    Core_retain, 
    Core_release, 
    Core_getCopyOfDescription,
    Core_equal,
    Core_hash    
};

static const CoreSetValueCallbacks CoreSetValueNullCallbacks =
{ 
    null, 
    null, 
    null,
    null,
    null
};



typedef enum CoreSetCallbacksType
{
    CORE_SET_NULL_CALLBACKS   = 1,
    CORE_SET_CORE_CALLBACKS   = 2,
    CORE_SET_CUSTOM_CALLBACKS = 3,
} CoreSetCallbacksType;



//
// Flags bits:
//  - key callbacks info is in 0-1 bits
//  - value callbacks info is in 2-3 bits
// 	- 
//
#define SET_TYPE_START       0
#define SET_TYPE_LENGTH      2  
#define VALUE_CALLBACKS_START       2
#define VALUE_CALLBACKS_LENGTH      2  


#define CORE_SET_MAX_THRESHOLD   (1 << 30)

#define EMPTY(me)   ((void *)(me)->marker)

#define IS_EMPTY(me, v) ((((CoreINT_U32) (v)) == (me)->marker) ? true: false)

#define DELETED(me)   ((void *)~((me)->marker))

#define IS_DELETED(me, v) ((((CoreINT_U32) (v)) == ~((me)->marker)) ? true: false)

#define IS_VALID(me, v) (!IS_EMPTY(me, v) && !IS_DELETED(me, v))



#define CORE_IS_SET(dict) CORE_VALIDATE_OBJECT(dict, CoreSetID)
#define CORE_IS_SET_RET0(dict) \
    do { if(!CORE_IS_SET(dict)) return ;} while (0)
#define CORE_IS_SET_RET1(dict, ret) \
    do { if(!CORE_IS_SET(dict)) return (ret);} while (0)



CORE_INLINE CoreSetType
__CoreSet_getType(CoreImmutableSetRef me)
{
    return (CoreSetType) CoreBitfield_getValue(
        ((const CoreRuntimeObject *) me)->info,
        SET_TYPE_START,
        SET_TYPE_LENGTH
    );
}

CORE_INLINE void
__CoreSet_setType(CoreImmutableSetRef me, CoreSetType type)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) me)->info,
        SET_TYPE_START,
        SET_TYPE_LENGTH,
        (CoreINT_U32) type
    );
}

CORE_INLINE CoreSetCallbacksType
__CoreSet_getValueCallbacksType(CoreImmutableSetRef me)
{
    return (CoreSetCallbacksType) CoreBitfield_getValue(
        ((const CoreRuntimeObject *) me)->info,
        VALUE_CALLBACKS_START,
        VALUE_CALLBACKS_LENGTH
    );
}

CORE_INLINE void
__CoreSet_setValueCallbacksType(
    CoreImmutableSetRef me, 
    CoreSetCallbacksType type
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
__CoreSet_valueCallbacksMatchNull(const CoreSetValueCallbacks * cb)
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
__CoreSet_valueCallbacksMatchCore(const CoreSetValueCallbacks * cb)
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

CORE_INLINE CoreSetValueCallbacks *
__CoreSet_getValueCallbacks(CoreImmutableSetRef me)
{
    const CoreSetValueCallbacks * result = null;
    
    switch (__CoreSet_getValueCallbacksType(me))
    {
        case CORE_SET_NULL_CALLBACKS:
            result = &CoreSetValueNullCallbacks;
            break;
        case CORE_SET_CORE_CALLBACKS:
            result = &CoreSetValueCoreCallbacks;
            break;
        default:
            result = (const CoreSetValueCallbacks *)
                ((CoreINT_U8 *) me + sizeof(struct __CoreSet));
            break;
    }
    
    return result;
}


CORE_INLINE CoreINT_U32
__CoreSet_getSizeOfType(
    CoreImmutableSetRef me,
    CoreSetType type)
{
    CoreINT_U32 result = 0;
    
    result += sizeof(struct __CoreSet);    
    if (__CoreSet_getValueCallbacksType(me) == 
        CORE_SET_CUSTOM_CALLBACKS)
    {
        result += sizeof(CoreSetValueCallbacks);
    }
    
    return result;
}


//
// Returns next power of two higher than the capacity
// threshold for the specified input capacity. Load factor is 3/4.
CORE_INLINE CoreINT_U32 
__CoreSet_roundUpThreshold(
    CoreINT_U32 capacity
)
{
	return (3 * capacity / 4);
}

//
// Returns next power of two higher than the capacity
// threshold for the specified input capacity. Load factor is 3/4.
CORE_INLINE CoreINT_U32 
__CoreSet_roundUpCapacity(
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
__CoreSet_rehashValue(
    CoreHashCode code
)
{
    return BobJenkinsHash(code);
}


CORE_INLINE CoreINT_U32
__CoreSet_getIndexForHashCode(
    CoreImmutableSetRef me,
    CoreHashCode hashCode)
{
    return (hashCode & (me->capacity - 1));
}


static CoreINT_U32
__CoreSet_getBucketForValue_1(
    CoreImmutableSetRef me,
    const void * value
)
{
    CoreINT_U32 result      = CORE_INDEX_NOT_FOUND;
    const void ** values    = me->values;
    CoreINT_U32 valueHash   = 0;
    CoreINT_U32 probe       = 0;
    CoreINT_U32 start       = 0;
       
    valueHash = __CoreSet_rehashValue((CoreHashCode) value);
    probe = __CoreSet_getIndexForHashCode(me, valueHash);
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
__CoreSet_getBucketForValue_2(
    CoreImmutableSetRef me,
    const void * value,
    CoreSetValueCallbacks * cb
)
{
    CoreINT_U32 result              = CORE_INDEX_NOT_FOUND;
    CoreINT_U32 valueHash           = 0;
    CoreINT_U32 probe               = 0;
    CoreINT_U32 start               = 0;
    const void ** values            = me->values;
    CoreBOOL (* opEqual)(CoreObjectRef, CoreObjectRef);
        
    valueHash = __CoreSet_rehashValue(cb->hash(value));
    probe = __CoreSet_getIndexForHashCode(me, valueHash);
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
__CoreSet_getBucketForValue(
    CoreImmutableSetRef me,
    const void * value
)
{
    CoreINT_U32 result;
    CoreSetCallbacksType cbType;
    
    cbType = __CoreSet_getValueCallbacksType(me);
    if (cbType == CORE_SET_NULL_CALLBACKS)
    {
        result = __CoreSet_getBucketForValue_1(me, value);
    }
    else
    {
        CoreSetValueCallbacks * cb;
        
        cb = __CoreSet_getValueCallbacks(me);
        if (cb->equal == null)
        {
            result = __CoreSet_getBucketForValue_1(me, value);
        }
        else
        {
            result = __CoreSet_getBucketForValue_2(me, value, cb);        
        }
    }
        
    return result;
}


static void
__CoreSet_findBuckets_1(
    CoreImmutableSetRef me,
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
    valueHash = __CoreSet_rehashValue((CoreHashCode) value);
    probe = __CoreSet_getIndexForHashCode(me, valueHash);
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
__CoreSet_findBuckets_2(
    CoreImmutableSetRef me,
    const void * value,
    CoreSetValueCallbacks * cb,
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
    valueHash = __CoreSet_rehashValue(cb->hash(value));
    probe = __CoreSet_getIndexForHashCode(me, valueHash);
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
__CoreSet_findBuckets(
    CoreImmutableSetRef me,
    const void * value,
    CoreINT_U32 * match,
    CoreINT_U32 * empty
)
{
    CoreSetCallbacksType cbType;

    cbType = __CoreSet_getValueCallbacksType(me);
    if (cbType == CORE_SET_NULL_CALLBACKS)
    {
        __CoreSet_findBuckets_1(me, value, match, empty);
    }
    else
    {
        CoreSetValueCallbacks * cb;

        cb = __CoreSet_getValueCallbacks(me);
        if (cb->equal == null)
        {
            __CoreSet_findBuckets_1(me, value, match, empty);
        }
        else
        {
            __CoreSet_findBuckets_2(me, value, cb, match, empty);
        }
    }
}


CORE_INLINE CoreBOOL
__CoreSet_isValueMagic(
    CoreImmutableSetRef me,
    const void * value
)
{
    return (IS_EMPTY(me, value) || IS_DELETED(me, value)) ? true: false;
}


static void
__CoreSet_changeMarker(    
    CoreImmutableSetRef me
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
	
    ((struct __CoreSet *) me)->marker = newMarker;
    
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
__CoreSet_transfer(
    CoreSetRef me, 
    const void ** oldValues, 
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
            
            __CoreSet_findBuckets(me, tmpValue, &match, &empty);
            if (empty != CORE_INDEX_NOT_FOUND)
            {
                me->values[empty] = tmpValue;
            }
        }
    }
}


static CoreBOOL
__CoreSet_expand(CoreSetRef me, CoreINT_U32 needed)
{
    CoreBOOL result = false;
    CoreINT_U32 neededCapacity = me->count + needed;
    
    if (neededCapacity <= me->maxThreshold)
    {
        const void ** oldValues = me->values;
        void ** newValues = null;
        CoreINT_U32 oldCapacity = me->capacity;
        CoreAllocatorRef allocator = null;
        
        me->capacity = __CoreSet_roundUpCapacity(neededCapacity); 
        me->threshold = min(
            __CoreSet_roundUpThreshold(me->capacity),
            me->maxThreshold
        );
        allocator = Core_getAllocator(me);
        newValues = CoreAllocator_allocate(
            allocator,
            me->capacity * sizeof(void *)
        );
        
        if (newValues != null)
        {
            // Reset the whole table of values.
            CoreINT_U32 idx;
            
            me->values = newValues;
            for (idx = 0; idx < me->capacity; idx++)
            {
                me->values[idx] = EMPTY(me);
            }
            
            // Now transfer content of old table to the new one.
            if (oldValues != null)
            {
                __CoreSet_transfer(me, oldValues, oldCapacity);            
                CoreAllocator_deallocate(allocator, (void *) oldValues);
            }
            result = true;
        }
    }
    
    return result; 				
}


CORE_INLINE CoreBOOL
__CoreSet_shouldShrink(CoreSetRef me)
{
    return false;
}


CORE_INLINE void
__CoreSet_shrink(CoreSetRef me)
{
    return ;
}


CORE_INLINE CoreBOOL
__CoreSet_addValue(
    CoreSetRef me, 
    const void * value)
{
    CoreBOOL result  = false;
    CoreBOOL ready   = true;
    
    if ((me->values == null) || (me->count >= me->threshold))
    {
        ready = __CoreSet_expand(me, 1);
    }
        
    if (ready)
    {
        CoreINT_U32 match;
        CoreINT_U32 empty;
         
        // Check the value for magic value.
        if (__CoreSet_isValueMagic(me, value))
        {
            __CoreSet_changeMarker(me);
        }
        
        __CoreSet_findBuckets(me, value, &match, &empty);
        if (match == CORE_INDEX_NOT_FOUND)
        {
            CoreSetValueCallbacks * valueCb;

            valueCb = __CoreSet_getValueCallbacks(me);
            if (valueCb->retain != null)
            {
                valueCb->retain(value);
            }
            me->values[empty] = value;
            me->count++;
            result = true;
        }
    }
    
    return result;
}


CORE_INLINE CoreBOOL
__CoreSet_removeValue(
    CoreSetRef me, 
    const void * value)
{
	CoreBOOL result = false;
    CoreINT_U32 index = __CoreSet_getBucketForValue(me, value);
        
    if (index != CORE_INDEX_NOT_FOUND)
    {
        CoreSetValueCallbacks * valueCb;

        valueCb = __CoreSet_getValueCallbacks(me);
        if (valueCb->release != null)
        {
            valueCb->release(me->values[index]);
        }
        
        me->count--;
        me->values[index] = DELETED(me);
        result = true;
               
        if (__CoreSet_shouldShrink(me))
        {
            __CoreSet_shrink(me);
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
    
    return result;
}


CORE_INLINE CoreBOOL
__CoreSet_replaceValue(
    CoreSetRef me, 
    const void * value)
{
	CoreBOOL result = false;
    CoreINT_U32 index = __CoreSet_getBucketForValue(me, value);
        
    if (index != CORE_INDEX_NOT_FOUND)
    {
        CoreSetValueCallbacks * valueCb;

        valueCb = __CoreSet_getValueCallbacks(me);
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
__CoreSet_clear(CoreSetRef me)
{
    CoreSetValueCallbacks * valueCb;
    
    valueCb = __CoreSet_getValueCallbacks(me);
    if ((me->values != null) && (valueCb->release != null))
    {
        CoreINT_U32 idx;
        
        for (idx = 0; idx < me->capacity; idx++)
        {
            const void * value = me->values[idx];
            if (IS_VALID(me, value))
            {
                if (valueCb->release != null)
                {
                    valueCb->release(value);
                }
            }
        }
    }
}


static void
__CoreSet_cleanup(CoreObjectRef me)
{
    struct __CoreSet * _me = (struct __CoreSet *) me;
        
    __CoreSet_clear(_me);
    switch(__CoreSet_getType(_me))
    {
        case CORE_SET_MUTABLE:
        {
            if (_me->values != null)
            {
                CoreAllocatorRef allocator = Core_getAllocator(me);
                CoreAllocator_deallocate(allocator, (void *) _me->values);
            }
            break;
        }
    }
}


static CoreBOOL
__CoreSet_equal(CoreObjectRef me, CoreObjectRef to)
{
    return false;
}


static CoreHashCode
__CoreSet_hash(CoreObjectRef me)
{
    return ((CoreImmutableSetRef) me)->count;
}


static CoreImmutableStringRef
__CoreSet_getCopyOfDescription(CoreObjectRef me)
{
    return null;
}




#ifdef __WIN32__
#define _strcat(dst, src, cnt) strcat_s(dst, cnt, src)
#else
#define _strcat(dst, src, cnt) strcat(dst, src)
#endif

static void
__CoreSet_collisionsForValue(
    CoreImmutableSetRef me,
    const void * value,
    CoreINT_U32 * collisions,
    CoreINT_U32 * comparisons
)
{
    CoreINT_U32 valueHash      = 0;
    CoreINT_U32 idx            = 0;
    CoreINT_U32 start          = 0;
    const void * cmpValue      = null;
    const void * realValue     = null;
	CoreSetValueCallbacks * cb = __CoreSet_getValueCallbacks(me);       
    
    valueHash = __CoreSet_rehashValue((cb->hash) ? cb->hash(value) : (CoreHashCode) value);
    idx = __CoreSet_getIndexForHashCode(me, valueHash);
    start = idx;
    *collisions = 0;
    *comparisons = 0;    
    
    do 
    {
        CoreINT_U32 cmpHash = 0;
        CoreINT_U32 cmpIdx = 0;
        cmpValue = me->values[idx];
        
        cmpHash = __CoreSet_rehashValue((cb->hash) ? cb->hash(cmpValue) : (CoreHashCode) cmpValue);
        cmpIdx = __CoreSet_getIndexForHashCode(me, cmpHash);
        
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
_CoreSet_description(CoreImmutableSetRef me)
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
		CoreSetValueCallbacks * cb = __CoreSet_getValueCallbacks(me);
		
		sprintf(s, "CoreSet <%p>\n{\n  capacity = %u, count = %u, \
threshold = %u\n  Buckets:\n", me, me->capacity, me->count, me->threshold);
		strcat(result, s);
            
        for (idx = 0; (me->values != null) && (idx < me->capacity); idx++)
        {
            CoreINT_U32 collisions = 0;
            CoreINT_U32 comparisons = 0;
            CoreINT_U32 hashCode = 0;
#if 0
            CoreCHAR_8 s2[150];
#endif
            
            if (!IS_EMPTY(me, me->values[idx]))
            {
                __CoreSet_collisionsForValue(
                    me, me->values[idx], &collisions, &comparisons
                );
                hashCode = __CoreSet_rehashValue(
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
					__CoreSet_getIndexForHashCode(me, hashCode),
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
            sizeof(struct __CoreSet) + 
            (me->capacity * 2) * sizeof(void *),
            CORE_SET_HASH_FUNC
        );
        strcat(result, s);
    }
	
	return result;
}






static CoreClassID CoreSetID = CORE_CLASS_ID_UNKNOWN;

static const CoreClass __CoreSetClass =
{
    0x00,                            // version
    "CoreSet",                       // name
    NULL,                            // init
    NULL,                            // copy
    __CoreSet_cleanup,               // cleanup
    __CoreSet_equal,                 // equal
    __CoreSet_hash,                  // hash
    __CoreSet_getCopyOfDescription   // getCopyOfDescription
};


/* CORE_PROTECTED */ void
CoreSet_initialize(void)
{
    CoreSetID = CoreRuntime_registerClass(&__CoreSetClass);
}



static struct __CoreSet *
__CoreSet_init(
    CoreAllocatorRef allocator,
    CoreINT_U32 capacity,
    const CoreSetValueCallbacks * valueCallbacks,
    CoreBOOL isMutable
)
{
    struct __CoreSet * result = null;
    CoreINT_U32 size = sizeof(struct __CoreSet);
    CoreSetType type;
    CoreSetCallbacksType valueCbType;
    
    if (isMutable)
    {
        type = CORE_SET_MUTABLE;
    }
    else
    {
        type = CORE_SET_IMMUTABLE;
        capacity = __CoreSet_roundUpCapacity(capacity);
        size += capacity * sizeof(const void *);
    }

    if (__CoreSet_valueCallbacksMatchNull(valueCallbacks))
    {
        valueCallbacks = &CoreSetValueNullCallbacks;
        valueCbType = CORE_SET_NULL_CALLBACKS;
    }
    else if (__CoreSet_valueCallbacksMatchCore(valueCallbacks))
    {
        valueCallbacks = &CoreSetValueCoreCallbacks;
        valueCbType = CORE_SET_CORE_CALLBACKS;    
    }
    else
    {
        valueCbType = CORE_SET_CUSTOM_CALLBACKS;
        size += sizeof(CoreSetValueCallbacks);
    }
        
    result = (struct __CoreSet *) CoreRuntime_createObject(
        allocator, CoreSetID, size
    );
    if (result != null)
    {
        __CoreSet_setType(result, type);
        __CoreSet_setValueCallbacksType(result, valueCbType);
        result->maxThreshold = (capacity == 0) 
            ? CORE_SET_MAX_THRESHOLD 
            : min(capacity, CORE_SET_MAX_THRESHOLD);
        result->count = 0;
        result->capacity = 0;
        result->marker = 0xdeadbeef;
        result->values = null;
        
        if (valueCbType == CORE_SET_CUSTOM_CALLBACKS)
        {
            CoreSetValueCallbacks * valueCb;
            valueCb = __CoreSet_getValueCallbacks(result);
            *valueCb = *valueCallbacks;                
        }
        
        switch (type)
        {
            case CORE_SET_IMMUTABLE:
            {
                CoreINT_U32 size;
                
                result->threshold = result->maxThreshold;
                size = __CoreSet_getSizeOfType(result, type);
                result->values = (void *) ((CoreINT_U8 *) result + 
                    sizeof(struct __CoreSet) + 
                    size);
                break;
            }
        }
    }
    
    return result;   
}


/* CORE_PUBLIC */ CoreSetRef
CoreSet_create(
    CoreAllocatorRef allocator,
    CoreINT_U32 capacity,
    const CoreSetValueCallbacks * valueCallbacks
)
{
    CoreSetRef result = __CoreSet_init(
        allocator,
        capacity,
        valueCallbacks,
        true
    );
    
    CORE_DUMP_MSG(
        CORE_LOG_TRACE | CORE_LOG_INFO, 
        "->%s: new object %p\n", __FUNCTION__, result
    );
    
    return result;
}


/* CORE_PUBLIC */ CoreSetRef
CoreSet_createImmutable(
    CoreAllocatorRef allocator,
    const void ** values,
    CoreINT_U32 count,
    const CoreSetValueCallbacks * valueCallbacks
)
{
    CoreSetRef result = null;
    
    CORE_ASSERT_RET1(
        null,
        (values != null) || (count == 0),
        CORE_LOG_ASSERT,
        "%s(): values cannot be NULL when count %u is > 0",
        __PRETTY_FUNCTION__
    );
    
    result = __CoreSet_init(
        allocator,
        count,
        valueCallbacks,
        false
    );
    if (result != null)
    {
        // temporarily switch to mutable variant and add all the keys-values
        CoreINT_U32 idx;
        
        __CoreSet_setType(result, CORE_SET_MUTABLE);
        for (idx = 0; idx < count; idx++)
        {
            CoreSet_addValue(result, values[idx]);
        }
        __CoreSet_setType(result, CORE_SET_IMMUTABLE);
    }
    
    CORE_DUMP_MSG(
        CORE_LOG_TRACE | CORE_LOG_INFO, 
        "->%s: new object %p\n", __FUNCTION__, result
    );
    
    return result;

}


/* CORE_PUBLIC */ CoreSetRef
CoreSet_createCopy(
    CoreAllocatorRef allocator,
    CoreImmutableSetRef set,
    CoreINT_U32 capacity
)
{
    CoreSetRef result;
    const CoreSetValueCallbacks * valueCallbacks;
    CoreINT_U32 count;

    CORE_IS_SET_RET1(set, null);
    
    count = CoreSet_getCount(set);
    valueCallbacks = __CoreSet_getValueCallbacks(set);
    
    result = __CoreSet_init(
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
        
        CoreSet_copyValues(set, values);
        if (capacity == 0)
        {
            __CoreSet_expand(result, count);
        }
        
        for (idx = 0; idx < count; idx++)
        {
            CoreSet_addValue(result, values[idx]);
        }       

        if (values != valuebuf)
        {
            CoreAllocator_deallocate(allocator, (void *) values);
        }
    }
    
    return result;    
}


/* CORE_PUBLIC */ CoreImmutableSetRef
CoreSet_createImmutableCopy(
    CoreAllocatorRef allocator,
    CoreImmutableSetRef set
)
{
    CoreSetRef result;
    const CoreSetValueCallbacks * valueCallbacks;
    CoreINT_U32 count;

    CORE_IS_SET_RET1(set, null);
    
    count = CoreSet_getCount(set);
    valueCallbacks = __CoreSet_getValueCallbacks(set);

    result = __CoreSet_init(
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
        
        CoreSet_copyValues(set, values);

        __CoreSet_setType(result, CORE_SET_MUTABLE);        
        for (idx = 0; idx < count; idx++)
        {
            CoreSet_addValue(result, values[idx]);
        }       
        __CoreSet_setType(result, CORE_SET_IMMUTABLE);
        
        if (values != valuebuf)
        {
            CoreAllocator_deallocate(allocator, (void *) values);
        }
    }
    
    return (CoreImmutableSetRef) result;    
}


/* CORE_PUBLIC */ CoreINT_U32
CoreSet_getCount(CoreImmutableSetRef me)
{
    CORE_IS_SET_RET1(me, 0);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    
    return me->count;
}


/* CORE_PUBLIC */ const void *
CoreSet_getValue(CoreImmutableSetRef me, const void * value)
{
    const void * result = null;
    CoreINT_U32 index = 0;

    CORE_IS_SET_RET1(me, null);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
      
    if (me->count > 0)
    {
        index = __CoreSet_getBucketForValue(me, value);         
        if (index != CORE_INDEX_NOT_FOUND)
        {
            result = me->values[index];
        }
    }
    
    return result;
}


/* CORE_PUBLIC */ CoreBOOL
CoreSet_getValueIfPresent(
    CoreImmutableSetRef me, 
    const void * candidate,
    const void ** value
)
{
    CoreBOOL result = false;
    CoreINT_U32 idx;

    CORE_IS_SET_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);

    if ((value != null) && (me->count > 0))
    {
        idx = __CoreSet_getBucketForValue(me, candidate);         
        if (idx != CORE_INDEX_NOT_FOUND)
        {
            *value = me->values[idx]; 
            result = true;
        }
    }
    
    return result;
}


/* CORE_PUBLIC */ CoreBOOL
CoreSet_containsValue(CoreImmutableSetRef me, const void * value)
{
    CoreBOOL result = false;
    CoreINT_U32 index;

    CORE_IS_SET_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);

    if (me->count > 0)
    {
        index = __CoreSet_getBucketForValue(me, value);         
        if (index != CORE_INDEX_NOT_FOUND)
        {
            result = true;
        }
    }
    
    return result;
}
   

/* CORE_PUBLIC */ void
CoreSet_copyValues(
    CoreImmutableSetRef me,
    const void ** values
)
{
    CORE_IS_SET_RET0(me);
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
_CoreSet_iterate(
    CoreImmutableSetRef me, 
    __CoreIteratorState * state,
    const void ** buffer,
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
CoreSet_addValue(
    CoreSetRef me, 
    const void * value)
{
    CORE_IS_SET_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreSet_getType(me) != CORE_SET_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );

    return __CoreSet_addValue(me, value);
}


/* CORE_PUBLIC */ CoreBOOL
CoreSet_removeValue(
    CoreSetRef me, 
    const void * value)
{
    CORE_IS_SET_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreSet_getType(me) != CORE_SET_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    
    return __CoreSet_removeValue(me, value);
}


/* CORE_PUBLIC */ CoreBOOL
CoreSet_replaceValue(
    CoreSetRef me, 
    const void * value)
{
    CORE_IS_SET_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreSet_getType(me) != CORE_SET_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    
    return __CoreSet_replaceValue(me, value);
}


/* CORE_PUBLIC */ void
CoreSet_clear(CoreSetRef me)
{
    CORE_IS_SET_RET0(me);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET0(
        (__CoreSet_getType(me) != CORE_SET_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );

    __CoreSet_clear(me);
} 



/* CORE_PUBLIC */ void
CoreSet_applyFunction(
    CoreImmutableSetRef me,
    CoreSetApplyFunction map,
    void * context    
)
{
    CoreINT_U32 idx, n;
    
    CORE_IS_SET_RET0(me);    
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

