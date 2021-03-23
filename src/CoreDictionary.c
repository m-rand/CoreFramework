

/*****************************************************************************
 *
 * Includes
 * 
 *****************************************************************************/
 
#include "CoreDictionary.h"
#include "CoreRuntime.h"
#include "CoreString.h"
#include <stdlib.h>



/*****************************************************************************
 *
 * Types definitions
 * 
 ****************************************************************************/

typedef enum CoreDictionaryType
{
    CORE_DICTIONARY_IMMUTABLE   = 1,
    CORE_DICTIONARY_MUTABLE     = 2,
} CoreDictionaryType;


struct __CoreDictionary
{
    CoreRuntimeObject core;
    CoreINT_U32	count;              // current number of entries
    CoreINT_U32	capacity;           // allocated capacity
    CoreINT_U32	threshold;          // max number of entries
    CoreINT_U32 maxThreshold;       // zero when unbounded
    CoreINT_U32 marker;
    const void ** keys;
    const void ** values;
    /* key callback struct -- if custom */
    /* value callback struct -- if custom */
    /* keys and values here -- if immutable */
};




/*****************************************************************************
 *
 * Macros and constants definitions
 * 
 ****************************************************************************/

#define CORE_DICTIONARY_HASH_FUNC "BobJenkinsHash"


const CoreDictionaryKeyCallbacks CoreDictionaryKeyCoreCallbacks =
{ 
    Core_retain, 
    Core_release, 
    Core_getCopyOfDescription,
    Core_equal,
    Core_hash
};

static const CoreDictionaryKeyCallbacks CoreDictionaryKeyNullCallbacks =
{ 
    null, 
    null, 
    null,
    null,
    null
};

const CoreDictionaryValueCallbacks CoreDictionaryValueCoreCallbacks =
{ 
    Core_retain, 
    Core_release, 
    Core_getCopyOfDescription,
    Core_equal
};

static const CoreDictionaryValueCallbacks CoreDictionaryValueNullCallbacks =
{ 
    null, 
    null, 
    null,
    null
};



typedef enum CoreDictionaryCallbacksType
{
    CORE_DICTIONARY_NULL_CALLBACKS   = 1,
    CORE_DICTIONARY_CORE_CALLBACKS   = 2,
    CORE_DICTIONARY_CUSTOM_CALLBACKS = 3,
} CoreDictionaryCallbacksType;



//
// Flags bits:
//  - key callbacks info is in 0-1 bits
//  - value callbacks info is in 2-3 bits
// 	- 
//
#define DICTIONARY_TYPE_START       0
#define DICTIONARY_TYPE_LENGTH      2  
#define KEY_CALLBACKS_START         2
#define KEY_CALLBACKS_LENGTH        2
#define VALUE_CALLBACKS_START       4
#define VALUE_CALLBACKS_LENGTH      2  


#define CORE_DICTIONARY_MAX_THRESHOLD   (1 << 30)

#define EMPTY(me)   ((void *)(me)->marker)

#define IS_EMPTY(me, v) ((((CoreINT_U32) (v)) == (me)->marker) ? true: false)

#define DELETED(me)   ((void *)~((me)->marker))

#define IS_DELETED(me, v) ((((CoreINT_U32) (v)) == ~((me)->marker)) ? true: false)

#define IS_VALID(me, v) (!IS_EMPTY(me, v) && !IS_DELETED(me, v))



#define CORE_IS_DICTIONARY(dict) CORE_VALIDATE_OBJECT(dict, CoreDictionaryID)
#define CORE_IS_DICTIONARY_RET0(dict) \
    do { if(!CORE_IS_DICTIONARY(dict)) return ;} while (0)
#define CORE_IS_DICTIONARY_RET1(dict, ret) \
    do { if(!CORE_IS_DICTIONARY(dict)) return (ret);} while (0)



CORE_INLINE CoreDictionaryType
__CoreDictionary_getType(CoreImmutableDictionaryRef me)
{
    return (CoreDictionaryType) CoreBitfield_getValue(
        ((const CoreRuntimeObject *) me)->info,
        DICTIONARY_TYPE_START,
        DICTIONARY_TYPE_LENGTH
    );
}

CORE_INLINE void
__CoreDictionary_setType(CoreImmutableDictionaryRef me, CoreDictionaryType type)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) me)->info,
        DICTIONARY_TYPE_START,
        DICTIONARY_TYPE_LENGTH,
        (CoreINT_U32) type
    );
}

CORE_INLINE CoreDictionaryCallbacksType
__CoreDictionary_getKeyCallbacksType(CoreImmutableDictionaryRef me)
{
    return (CoreDictionaryCallbacksType) CoreBitfield_getValue(
        ((const CoreRuntimeObject *) me)->info,
        KEY_CALLBACKS_START,
        KEY_CALLBACKS_LENGTH
    );
}

CORE_INLINE void
__CoreDictionary_setKeyCallbacksType(
    CoreImmutableDictionaryRef me, 
    CoreDictionaryCallbacksType type
)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) me)->info,
        KEY_CALLBACKS_START,
        KEY_CALLBACKS_LENGTH,
        (CoreINT_U32) type
    );
}

CORE_INLINE CoreDictionaryCallbacksType
__CoreDictionary_getValueCallbacksType(CoreImmutableDictionaryRef me)
{
    return (CoreDictionaryCallbacksType) CoreBitfield_getValue(
        ((const CoreRuntimeObject *) me)->info,
        VALUE_CALLBACKS_START,
        VALUE_CALLBACKS_LENGTH
    );
}

CORE_INLINE void
__CoreDictionary_setValueCallbacksType(
    CoreImmutableDictionaryRef me, 
    CoreDictionaryCallbacksType type
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
__CoreDictionary_keyCallbacksMatchNull(const CoreDictionaryKeyCallbacks * cb)
{
    CoreBOOL result = false;
    
    result = (
        (cb == null) ||
        ((cb->retain == null) &&
         (cb->release == null) &&
         (cb->getCopyOfDescription == null) &&
         (cb->equal == null) &&
         (cb->hash == null))
    );
    
    return result;
}

CORE_INLINE CoreBOOL
__CoreDictionary_keyCallbacksMatchCore(const CoreDictionaryKeyCallbacks * cb)
{
    CoreBOOL result = false;
    
    result = (
        (cb != null) &&
        ((cb->retain == Core_retain) &&
         (cb->release == Core_release) &&
         (cb->getCopyOfDescription == Core_getCopyOfDescription) &&
         (cb->equal == Core_equal) &&
         (cb->hash == Core_hash))
    );
    
    return result;
}

CORE_INLINE CoreBOOL
__CoreDictionary_valueCallbacksMatchNull(const CoreDictionaryValueCallbacks * cb)
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
__CoreDictionary_valueCallbacksMatchCore(const CoreDictionaryValueCallbacks * cb)
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

CORE_INLINE const CoreDictionaryKeyCallbacks *
__CoreDictionary_getKeyCallbacks(CoreImmutableDictionaryRef me)
{
    const CoreDictionaryKeyCallbacks * result = null;
    
    switch (__CoreDictionary_getKeyCallbacksType(me))
    {
        case CORE_DICTIONARY_NULL_CALLBACKS:
            result = &CoreDictionaryKeyNullCallbacks;
            break;
        case CORE_DICTIONARY_CORE_CALLBACKS:
            result = &CoreDictionaryKeyCoreCallbacks;
            break;
        case CORE_DICTIONARY_CUSTOM_CALLBACKS:
        default:
            result = (CoreDictionaryKeyCallbacks *)
                ((CoreINT_U8 *) me + sizeof(struct __CoreDictionary));
            break;
    }
    
    return result;
}

CORE_INLINE const CoreDictionaryValueCallbacks *
__CoreDictionary_getValueCallbacks(CoreImmutableDictionaryRef me)
{
    const CoreDictionaryValueCallbacks * result = null;
    
    switch (__CoreDictionary_getValueCallbacksType(me))
    {
        case CORE_DICTIONARY_NULL_CALLBACKS:
            result = &CoreDictionaryValueNullCallbacks;
            break;
        case CORE_DICTIONARY_CORE_CALLBACKS:
            result = &CoreDictionaryValueCoreCallbacks;
            break;
        case CORE_DICTIONARY_CUSTOM_CALLBACKS:
        default:
            result = (CoreDictionaryValueCallbacks *)
                ((CoreINT_U8 *) me + sizeof(struct __CoreDictionary));
            if (__CoreDictionary_getKeyCallbacksType(me) == 
                CORE_DICTIONARY_CUSTOM_CALLBACKS)
            {
                result = (CoreDictionaryValueCallbacks *)
                    ((CoreINT_U8 *) result + sizeof(CoreDictionaryKeyCallbacks));
            }
            break;
    }
    
    return result;
}

CORE_INLINE void
__CoreDictionary_setKeyCallbacks(
    CoreImmutableDictionaryRef me, const CoreDictionaryKeyCallbacks * cb)
{
    switch (__CoreDictionary_getKeyCallbacksType(me))
    {
        case CORE_DICTIONARY_CUSTOM_CALLBACKS:
        {
            CoreDictionaryKeyCallbacks * mem = (CoreDictionaryKeyCallbacks *)
                ((CoreINT_U8 *) me + sizeof(struct __CoreDictionary));
            *mem = *cb;
            break;            
        }
        default:
            break;
    }
}

CORE_INLINE void
__CoreDictionary_setValueCallbacks(
    CoreImmutableDictionaryRef me, const CoreDictionaryValueCallbacks * cb)
{
    switch (__CoreDictionary_getValueCallbacksType(me))
    {
        case CORE_DICTIONARY_CUSTOM_CALLBACKS:
        {
            CoreDictionaryValueCallbacks * mem = (CoreDictionaryValueCallbacks *)
                ((CoreINT_U8 *) me + sizeof(struct __CoreDictionary));
            if (__CoreDictionary_getKeyCallbacksType(me) == 
                CORE_DICTIONARY_CUSTOM_CALLBACKS)
            {
                mem = (CoreDictionaryValueCallbacks *)
                    ((CoreINT_U8 *) mem + sizeof(CoreDictionaryKeyCallbacks));
            }
            *mem = *cb;
            break;
        }
        default:
            break;
    }
}


CORE_INLINE CoreINT_U32
__CoreDictionary_getSizeOfType(
    CoreImmutableDictionaryRef me,
    CoreDictionaryType type)
{
    CoreINT_U32 result = 0;
    
    result += sizeof(struct __CoreDictionary);    
    if (__CoreDictionary_getKeyCallbacksType(me) == 
        CORE_DICTIONARY_CUSTOM_CALLBACKS)
    {
        result += sizeof(CoreDictionaryKeyCallbacks);
    }
    if (__CoreDictionary_getValueCallbacksType(me) == 
        CORE_DICTIONARY_CUSTOM_CALLBACKS)
    {
        result += sizeof(CoreDictionaryValueCallbacks);
    }
    
    return result;
}


CORE_INLINE CoreINT_U32 
__CoreDictionary_roundUpThreshold(
    CoreINT_U32 capacity
)
{
	return (3 * capacity / 4);
}

//
// Returns next power of two higher than the capacity
// threshold for the specified input capacity. Load factor is 3/4.
CORE_INLINE CoreINT_U32 
__CoreDictionary_roundUpCapacity(
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
__CoreDictionary_rehashKey(
    CoreHashCode code
)
{
    return BobJenkinsHash(code);
}


CORE_INLINE CoreINT_U32
__CoreDictionary_getIndexForHashCode(
    CoreImmutableDictionaryRef me,
    CoreHashCode hashCode)
{
    return (hashCode & (me->capacity - 1));
}


static CoreINT_U32
__CoreDictionary_getBucketForKey_1(
    CoreImmutableDictionaryRef me,
    const void * key
)
{
    CoreINT_U32 result      = CORE_INDEX_NOT_FOUND;
    const void ** keys      = me->keys;
    CoreINT_U32 keyHash     = 0;
    CoreINT_U32 probe       = 0;
    CoreINT_U32 start       = 0;
       
    keyHash = __CoreDictionary_rehashKey((CoreHashCode) key);
    probe = __CoreDictionary_getIndexForHashCode(me, keyHash);
    start = probe;

    for ( ; !IS_EMPTY(me, keys[probe]) && (probe < me->capacity); probe++)
    {
        if (!IS_DELETED(me, keys[probe]))
        {
            if (keys[probe] == key)
            {
                result = probe;
                break;
            }
        }
    }
    
    if (result == CORE_INDEX_NOT_FOUND)
    {
        for (probe = 0; !IS_EMPTY(me, keys[probe]) && (probe < start); probe++)
        {
            if (!IS_DELETED(me, keys[probe]))
            {
                if (keys[probe] == key)
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
__CoreDictionary_getBucketForKey_2(
    CoreImmutableDictionaryRef me,
    const void * key,
    const CoreDictionaryKeyCallbacks * cb
)
{
    CoreINT_U32 result              = CORE_INDEX_NOT_FOUND;
    CoreINT_U32 keyHash             = 0;
    CoreINT_U32 probe               = 0;
    CoreINT_U32 start               = 0;
    const void ** keys              = me->keys;
    CoreDictionary_equalCallback opEqual;
        
    keyHash = __CoreDictionary_rehashKey(cb->hash(key));
    probe = __CoreDictionary_getIndexForHashCode(me, keyHash);
    opEqual = cb->equal;
    start = probe;
    
    for ( ; !IS_EMPTY(me, keys[probe]) && (probe < me->capacity); probe++)
    {
        if (!IS_DELETED(me, keys[probe]))
        {
            if ((keys[probe] == key) || opEqual(key, keys[probe]))
            {
                result = probe;
                break;
            }
        }
    }
    
    if (result == CORE_INDEX_NOT_FOUND)
    {
        for (probe = 0; !IS_EMPTY(me, keys[probe]) && (probe < start); probe++)
        {
            if (!IS_DELETED(me, keys[probe]))
            {
                if ((keys[probe] == key) || opEqual(key, keys[probe]))
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
__CoreDictionary_getBucketForKey(
    CoreImmutableDictionaryRef me,
    const void * key
)
{
    CoreINT_U32 result;
    CoreDictionaryCallbacksType cbType;
    
    cbType = __CoreDictionary_getKeyCallbacksType(me);
    if (cbType == CORE_DICTIONARY_NULL_CALLBACKS)
    {
        result = __CoreDictionary_getBucketForKey_1(me, key);
    }
    else
    {
        const CoreDictionaryKeyCallbacks * cb;
        
        cb = __CoreDictionary_getKeyCallbacks(me);
        if (cb->equal == null)
        {
            result = __CoreDictionary_getBucketForKey_1(me, key);
        }
        else
        {
            result = __CoreDictionary_getBucketForKey_2(me, key, cb);        
        }
    }
        
    return result;
}


static void
__CoreDictionary_findBuckets_1(
    CoreImmutableDictionaryRef me,
    const void * key,
    CoreINT_U32 * match,
    CoreINT_U32 * empty
)
{
    CoreINT_U32 keyHash     = 0;
    CoreINT_U32 probe       = 0;
    CoreINT_U32 start       = 0;
    const void ** keys      = me->keys;      
       
    *match = CORE_INDEX_NOT_FOUND;
    *empty = CORE_INDEX_NOT_FOUND;    
    keyHash = __CoreDictionary_rehashKey((CoreHashCode) key);
    probe = __CoreDictionary_getIndexForHashCode(me, keyHash);
    start = probe;

    // really hard to keep Misra-C instructions...
    for ( ; probe < me->capacity; probe++)
    {
        if (IS_EMPTY(me, keys[probe]))
        {
            if (*empty == CORE_INDEX_NOT_FOUND)
            {
                *empty = probe;
            }
            break;                 
        }
        else if (IS_DELETED(me, keys[probe]))
        {
            if (*empty == CORE_INDEX_NOT_FOUND)
            {
                *empty = probe;
            }                
        }
        else
        {
            if (key == keys[probe])
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
            if (IS_EMPTY(me, keys[probe]))
            {
                if (*empty == CORE_INDEX_NOT_FOUND)
                {
                    *empty = probe;
                }
                break;                 
            }
            else if (IS_DELETED(me, keys[probe]))
            {
                if (*empty == CORE_INDEX_NOT_FOUND)
                {
                    *empty = probe;
                }                
            }
            else
            {
                if (key == keys[probe])
                {
                    *match = probe;
                    break;            
                }
            }
        }
    }
}


static void
__CoreDictionary_findBuckets_2(
    CoreImmutableDictionaryRef me,
    const void * key,
    const CoreDictionaryKeyCallbacks * cb,
    CoreINT_U32 * match,
    CoreINT_U32 * empty
)
{
    CoreINT_U32 keyHash             = 0;
    CoreINT_U32 probe               = 0;
    CoreINT_U32 start               = 0;
    const void ** keys              = me->keys;    
    CoreBOOL (* opEqual)(CoreObjectRef, CoreObjectRef);
               
    *match = CORE_INDEX_NOT_FOUND;
    *empty = CORE_INDEX_NOT_FOUND;
    opEqual = cb->equal;        
    keyHash = __CoreDictionary_rehashKey(cb->hash(key));
    probe = __CoreDictionary_getIndexForHashCode(me, keyHash);
    start = probe;

    // really hard to keep Misra-C instructions...
    for ( ; probe < me->capacity; probe++)
    {
        if (IS_EMPTY(me, keys[probe]))
        {
            if (*empty == CORE_INDEX_NOT_FOUND)
            {
                *empty = probe;
            }
            break;                 
        }
        else if (IS_DELETED(me, keys[probe]))
        {
            if (*empty == CORE_INDEX_NOT_FOUND)
            {
                *empty = probe;
            }                
        }
        else
        {
            if (opEqual(key, keys[probe]))
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
            if (IS_EMPTY(me, keys[probe]))
            {
                if (*empty == CORE_INDEX_NOT_FOUND)
                {
                    *empty = probe;
                }
                break;                 
            }
            else if (IS_DELETED(me, keys[probe]))
            {
                if (*empty == CORE_INDEX_NOT_FOUND)
                {
                    *empty = probe;
                }                
            }
            else
            {
                if (opEqual(key, keys[probe]))
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
__CoreDictionary_findBuckets(
    CoreImmutableDictionaryRef me,
    const void * key,
    CoreINT_U32 * match,
    CoreINT_U32 * empty
)
{
    CoreDictionaryCallbacksType cbType;

    cbType = __CoreDictionary_getKeyCallbacksType(me);
    if (cbType == CORE_DICTIONARY_NULL_CALLBACKS)
    {
        __CoreDictionary_findBuckets_1(me, key, match, empty);
    }
    else
    {
        const CoreDictionaryKeyCallbacks * cb;

        cb = __CoreDictionary_getKeyCallbacks(me);
        if (cb->equal == null)
        {
            __CoreDictionary_findBuckets_1(me, key, match, empty);
        }
        else
        {
            __CoreDictionary_findBuckets_2(me, key, cb, match, empty);
        }
    }
}


CORE_INLINE CoreBOOL
__CoreDictionary_isKeyMagic(
    CoreImmutableDictionaryRef me,
    const void * key
)
{
    return (IS_EMPTY(me, key) || IS_DELETED(me, key)) ? true: false;
}


static void
__CoreDictionary_changeMarker(    
    CoreImmutableDictionaryRef me
)
{
    CoreBOOL hit;
	CoreINT_U32 idx;
    CoreINT_U32 newMarker = me->marker;
	CoreINT_U32 n = me->capacity;
	const void ** keys = me->keys;
    
	// Find the new empty.
    do
	{
        newMarker--;
		hit = false;
		for (idx = 0; idx < n; idx++)
		{
			if ((newMarker == (CoreINT_U32) keys[idx]) ||
			    (~newMarker == (CoreINT_U32) keys[idx]))             
			{
				hit = true;
				break;
			}
		}
	}
	while (hit);
	
    ((struct __CoreDictionary *) me)->marker = newMarker;
    
	// Update the table with new empty.
    for (idx = 0; idx < n; idx++)
	{
		if (me->marker == (CoreINT_U32) keys[idx])
		{
			keys[idx] = (void *) EMPTY(me);
		}
	}
}


CORE_INLINE CoreBOOL
__CoreDictionary_containsValue(CoreImmutableDictionaryRef me, const void * value)
{
    CoreBOOL result = false;
    CoreDictionaryCallbacksType cbType;
    const CoreDictionaryValueCallbacks * cb;
    
    cbType = __CoreDictionary_getValueCallbacksType(me);
    cb = __CoreDictionary_getValueCallbacks(me);
    if ((cbType == CORE_DICTIONARY_NULL_CALLBACKS) ||
        (cb->equal == null))
    {
        CoreINT_U32 idx, n;
        
        for (idx = 0, n = me->capacity; idx < n; idx++)
        {
            if (IS_VALID(me, me->keys[idx]) && (value == me->values[idx]))
            {
                result = true;
                break;
            }
        }
    }
    else
    {
        CoreINT_U32 idx;
        CoreBOOL (* opEqual)(CoreObjectRef, CoreObjectRef);
        
        opEqual = cb->equal;
        for (idx = 0; idx < me->capacity; idx++)
        {
            if (IS_VALID(me, me->keys[idx]))
            {
                if ((value == me->values[idx]) 
                    || opEqual(value, me->values[idx]))
                {
                    result = true;
                    break;
                }
            }
        }
    }
    
    return result;
}


CORE_INLINE void
__CoreDictionary_transfer(
    CoreDictionaryRef me, 
    const void ** oldKeys, 
    const void ** oldValues,
    CoreINT_U32 oldCapacity)
{
    CoreINT_U32 idx;
    
    for (idx = 0; idx < oldCapacity; idx++)
    {
        const void * tmpKey = oldKeys[idx];
        
        if (IS_VALID(me, tmpKey))
        {
            CoreINT_U32 match;
            CoreINT_U32 empty;
            
            __CoreDictionary_findBuckets(me, tmpKey, &match, &empty);
            if (empty != CORE_INDEX_NOT_FOUND)
            {
                me->keys[empty] = tmpKey;
                me->values[empty] = oldValues[idx];
            }
        }
    }
}


static CoreBOOL
__CoreDictionary_expand(CoreDictionaryRef me, CoreINT_U32 needed)
{
    CoreBOOL result = false;
    CoreINT_U32 neededCapacity = me->count + needed;
    
    if (neededCapacity <= me->maxThreshold)
    {
        const void ** oldKeys = me->keys;
        const void ** oldValues = me->values;
        void ** newKeys = null;
        void ** newValues = null;
        CoreINT_U32 oldCapacity = me->capacity;
        CoreAllocatorRef allocator = null;
        
        me->capacity = __CoreDictionary_roundUpCapacity(neededCapacity); 
        me->threshold = min(
            __CoreDictionary_roundUpThreshold(me->capacity),
            me->maxThreshold
        );
        allocator = Core_getAllocator(me);
        newKeys = CoreAllocator_allocate(
            allocator,
            me->capacity * sizeof(void *)
        );
        newValues = CoreAllocator_allocate(
            allocator,
            me->capacity * sizeof(void *)
        );
        
        if ((newKeys != null) && (newValues != null))
        {
            // Reset the whole table of keys.
            CoreINT_U32 idx;
            
            me->keys = newKeys;
            me->values = newValues;
            for (idx = 0; idx < me->capacity; idx++)
            {
                me->keys[idx] = EMPTY(me);
            }
            
            // Now transfer content of the old table to the new one.
            if (oldKeys != null)
            {
                __CoreDictionary_transfer(me, oldKeys, oldValues, oldCapacity);            
                CoreAllocator_deallocate(allocator, (void *) oldKeys);
                CoreAllocator_deallocate(allocator, (void *) oldValues);
            }
            result = true;
        }
    }
    
    return result; 				
}


CORE_INLINE CoreBOOL
__CoreDictionary_shouldShrink(CoreDictionaryRef me)
{
    return false;
}


CORE_INLINE void
__CoreDictionary_shrink(CoreDictionaryRef me)
{
    return ;
}


CORE_INLINE CoreBOOL
__CoreDictionary_addValue(
    CoreDictionaryRef me, 
    const void * key,
    const void * value)
{
    CoreBOOL result  = false;
    CoreBOOL ready   = true;
    
    if ((me->keys == null) || (me->count >= me->threshold))
    {
        ready = __CoreDictionary_expand(me, 1);
    }
        
    if (ready)
    {
        CoreINT_U32 match;
        CoreINT_U32 empty;
         
        // Check the key for magic value.
        if (CORE_UNLIKELY(__CoreDictionary_isKeyMagic(me, key)))
        {
            __CoreDictionary_changeMarker(me);
        }
        
        __CoreDictionary_findBuckets(me, key, &match, &empty);
        if (match == CORE_INDEX_NOT_FOUND)
        {
            const CoreDictionaryKeyCallbacks * keyCb;
            const CoreDictionaryValueCallbacks * valueCb;

            keyCb = __CoreDictionary_getKeyCallbacks(me);
            valueCb = __CoreDictionary_getValueCallbacks(me);
            if (keyCb->retain != null)
            {
                keyCb->retain(key);
            }
            if (valueCb->retain != null)
            {
                valueCb->retain(value);
            }
            me->keys[empty] = key;
            me->values[empty] = value;
            me->count++;
            result = true;
        }
    }
    
    return result;
}


CORE_INLINE CoreBOOL
__CoreDictionary_removeValue(
    CoreDictionaryRef me, 
    const void * key)
{
	CoreBOOL result = false;
    CoreINT_U32 index = __CoreDictionary_getBucketForKey(me, key);
        
    if (index != CORE_INDEX_NOT_FOUND)
    {
        const CoreDictionaryKeyCallbacks * keyCb;
        const CoreDictionaryValueCallbacks * valueCb;

        keyCb = __CoreDictionary_getKeyCallbacks(me);
        valueCb = __CoreDictionary_getValueCallbacks(me);
        if (keyCb->release != null)
        {
            keyCb->release(key);
        }
        if (valueCb->release != null)
        {
            valueCb->release(me->values[index]);
        }
        
        me->count--;
        me->keys[index] = DELETED(me);
        result = true;
               
        if (__CoreDictionary_shouldShrink(me))
        {
            __CoreDictionary_shrink(me);
        }
        else
        {
            // All deleted slots followed by an empty slot will be converted
            // to an empty slot.
            if ((index < me->capacity) && (IS_EMPTY(me, me->keys[index + 1])))
            {
                CoreINT_S32 idx = (CoreINT_S32) index;
                for ( ; (idx >= 0) && IS_DELETED(me, me->keys[idx]); idx--)
                {
                    me->keys[idx] = EMPTY(me);
                }
            }
        }
    }
    
    return result;
}


CORE_INLINE CoreBOOL
__CoreDictionary_replaceValue(
    CoreDictionaryRef me, 
    const void * key,
    const void * value)
{
	CoreBOOL result = false;
    CoreINT_U32 index = __CoreDictionary_getBucketForKey(me, key);
        
    if (index != CORE_INDEX_NOT_FOUND)
    {
        const CoreDictionaryValueCallbacks * valueCb;

        valueCb = __CoreDictionary_getValueCallbacks(me);
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
__CoreDictionary_clear(CoreDictionaryRef me)
{
    const CoreDictionaryKeyCallbacks * keyCb;
    const CoreDictionaryValueCallbacks * valueCb;
    
    keyCb = __CoreDictionary_getKeyCallbacks(me);
    valueCb = __CoreDictionary_getValueCallbacks(me);
    if ((me->keys != null) 
        && ((keyCb->release != null) || (valueCb->release != null)))
    {
        CoreINT_U32 idx;
        
        for (idx = 0; idx < me->capacity; idx++)
        {
            const void * key = me->keys[idx];
            if (IS_VALID(me, key))
            {
                if (keyCb->release != null)
                {
                    keyCb->release(key);
                }
                if (valueCb->release != null)
                {
                    valueCb->release(me->values[idx]);
                }
            }
        }
    }
}


static void
__CoreDictionary_cleanup(CoreObjectRef me)
{
    struct __CoreDictionary * _me = (struct __CoreDictionary *) me;
        
    __CoreDictionary_clear(_me);
    switch(__CoreDictionary_getType(_me))
    {
        case CORE_DICTIONARY_MUTABLE:
        {
            if (_me->keys != null)
            {
                CoreAllocatorRef allocator = Core_getAllocator(me);
                CoreAllocator_deallocate(allocator, (void *) _me->keys);
                CoreAllocator_deallocate(allocator, (void *) _me->values);
            }
            break;
        }
    }
}


static CoreBOOL
__CoreDictionary_equal(CoreObjectRef me, CoreObjectRef to)
{
    return false;
}


static CoreHashCode
__CoreDictionary_hash(CoreObjectRef me)
{
    return ((CoreImmutableDictionaryRef) me)->count;
}


static CoreImmutableStringRef
__CoreDictionary_getCopyOfDescription(CoreObjectRef me)
{
    return null;
}




#ifdef __WIN32__
#define _strcat(dst, src, cnt) strcat_s(dst, cnt, src)
#else
#define _strcat(dst, src, cnt) strcat(dst, src)
#endif

static void
__CoreDictionary_collisionsForKey(
    CoreImmutableDictionaryRef me,
    const void * key,
    CoreINT_U32 * collisions,
    CoreINT_U32 * comparisons
)
{
    CoreINT_U32 keyHash     = 0;
    CoreINT_U32 idx         = 0;
    CoreINT_U32 start       = 0;
    const void * cmpKey      = null;
    const void * realKey    = null;
	const CoreDictionaryKeyCallbacks * cb = __CoreDictionary_getKeyCallbacks(me);       
    
    keyHash = __CoreDictionary_rehashKey((cb->hash) ? cb->hash(key) : (CoreHashCode) key);
    idx = __CoreDictionary_getIndexForHashCode(me, keyHash);
    start = idx;
    *collisions = 0;
    *comparisons = 0;    
    
    do 
    {
        CoreINT_U32 cmpHash = 0;
        CoreINT_U32 cmpIdx = 0;
        cmpKey = me->keys[idx];
        
        cmpHash = __CoreDictionary_rehashKey((cb->hash) ? cb->hash(cmpKey) : (CoreHashCode) cmpKey);
        cmpIdx = __CoreDictionary_getIndexForHashCode(me, cmpHash);
        
        *comparisons += 1;
        if ((cmpKey == realKey) || (cb->equal) ? (realKey, cmpKey) : false)
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
    while (!IS_EMPTY(me, cmpKey) && (idx != start));
}

/* CORE_PROTECTED */ char * 
_CoreDictionary_description(CoreImmutableDictionaryRef me)
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
		const CoreDictionaryKeyCallbacks * cb = __CoreDictionary_getKeyCallbacks(me);
		
		sprintf(s, "CoreDictionary <%p>\n{\n  capacity = %u, count = %u, \
threshold = %u\n  Buckets:\n", me, me->capacity, me->count, me->threshold);
		strcat(result, s);
            
        for (idx = 0; (me->keys != null) && (idx < me->capacity); idx++)
        {
            CoreINT_U32 collisions = 0;
            CoreINT_U32 comparisons = 0;
            CoreINT_U32 hashCode = 0;
#if 0
            CoreCHAR_8 s2[150];
#endif            
            if (!IS_EMPTY(me, me->keys[idx]))
            {
                __CoreDictionary_collisionsForKey(
                    me, me->keys[idx], &collisions, &comparisons
                );
                hashCode = __CoreDictionary_rehashKey(
                    (cb->hash) ? cb->hash(me->keys[idx]) : (CoreHashCode) me->keys[idx]
                );
#if 0
                sprintf(
                    s2, 
					"  [%5d] - key[%p], hash 0x%08x, idx[%5d], value[%p], "
                    "coll %u, comp %u\n", 
                    idx, 
                    me->keys[idx], 
                    hashCode,
					__CoreDictionary_getIndexForHashCode(me, hashCode),
                    me->values[idx],
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
            sizeof(struct __CoreDictionary) + 
            (me->capacity * 2) * sizeof(void *),
            CORE_DICTIONARY_HASH_FUNC
        );
        strcat(result, s);
    }
	
	return result;
}






static CoreClassID CoreDictionaryID = CORE_CLASS_ID_UNKNOWN;

static const CoreClass __CoreDictionaryClass =
{
    0x00,                                   // version
    "CoreDictionary",                       // name
    NULL,                                   // init
    NULL,                                   // copy
    __CoreDictionary_cleanup,               // cleanup
    __CoreDictionary_equal,                 // equal
    __CoreDictionary_hash,                  // hash
    __CoreDictionary_getCopyOfDescription   // getCopyOfDescription
};


/* CORE_PROTECTED */ void
CoreDictionary_initialize(void)
{
    CoreDictionaryID = CoreRuntime_registerClass(&__CoreDictionaryClass);
}

/* CORE_PUBLIC */ CoreClassID
CoreDictionary_getClassID(void)
{
    return CoreDictionaryID;
}



static struct __CoreDictionary *
__CoreDictionary_init(
    CoreAllocatorRef allocator,
    CoreINT_U32 capacity,
    const CoreDictionaryKeyCallbacks * keyCallbacks,
    const CoreDictionaryValueCallbacks * valueCallbacks,
    CoreBOOL isMutable
)
{
    struct __CoreDictionary * result = null;
    CoreINT_U32 size = sizeof(struct __CoreDictionary);
    CoreDictionaryType type;
    CoreDictionaryCallbacksType keyCbType;
    CoreDictionaryCallbacksType valueCbType;
    
    if (isMutable)
    {
        type = CORE_DICTIONARY_MUTABLE;
    }
    else
    {
        type = CORE_DICTIONARY_IMMUTABLE;
        capacity = __CoreDictionary_roundUpCapacity(capacity);
        size += 2 * capacity * sizeof(const void *);
    }

    if (__CoreDictionary_keyCallbacksMatchNull(keyCallbacks))
    {
        keyCallbacks = &CoreDictionaryKeyNullCallbacks;
        keyCbType = CORE_DICTIONARY_NULL_CALLBACKS;
    }
    else if (__CoreDictionary_keyCallbacksMatchCore(keyCallbacks))
    {
        keyCallbacks = &CoreDictionaryKeyCoreCallbacks;
        keyCbType = CORE_DICTIONARY_CORE_CALLBACKS;    
    }
    else
    {
        keyCbType = CORE_DICTIONARY_CUSTOM_CALLBACKS;
        size += sizeof(CoreDictionaryKeyCallbacks);
    }
    
    if (__CoreDictionary_valueCallbacksMatchNull(valueCallbacks))
    {
        valueCallbacks = &CoreDictionaryValueNullCallbacks;
        valueCbType = CORE_DICTIONARY_NULL_CALLBACKS;
    }
    else if (__CoreDictionary_valueCallbacksMatchCore(valueCallbacks))
    {
        valueCallbacks = &CoreDictionaryValueCoreCallbacks;
        valueCbType = CORE_DICTIONARY_CORE_CALLBACKS;    
    }
    else
    {
        valueCbType = CORE_DICTIONARY_CUSTOM_CALLBACKS;
        size += sizeof(CoreDictionaryValueCallbacks);
    }
    
    result = (struct __CoreDictionary *) CoreRuntime_createObject(
        allocator, CoreDictionaryID, size
    );
    if (result != null)
    {
        __CoreDictionary_setType(result, type);
        __CoreDictionary_setKeyCallbacksType(result, keyCbType);
        __CoreDictionary_setValueCallbacksType(result, valueCbType);
        result->maxThreshold = (capacity == 0) 
            ? CORE_DICTIONARY_MAX_THRESHOLD 
            : min(capacity, CORE_DICTIONARY_MAX_THRESHOLD);
        result->count = 0;
        result->capacity = 0;
        result->marker = 0xdeadbeef;
        result->keys = null;
        result->values = null;
        
        if (keyCbType == CORE_DICTIONARY_CUSTOM_CALLBACKS)
        {
            __CoreDictionary_setKeyCallbacks(result, keyCallbacks);
        }
        if (valueCbType == CORE_DICTIONARY_CUSTOM_CALLBACKS)
        {
            __CoreDictionary_setValueCallbacks(result, valueCallbacks);        
        }
        
        switch (type)
        {
            case CORE_DICTIONARY_IMMUTABLE:
            {
                CoreINT_U32 size;
                
                result->threshold = result->maxThreshold;
                size = __CoreDictionary_getSizeOfType(result, type);
                result->keys = (void *) ((CoreINT_U8 *) result + 
                    sizeof(struct __CoreDictionary) + 
                    size);
                result->values = (void *) ((CoreINT_U8 *) result +
                    sizeof(struct __CoreDictionary) + 
                    size + 
                    capacity * sizeof(const void *));
                break;
            }
        }
    }
    
    return result;   
}


/* CORE_PUBLIC */ CoreDictionaryRef
CoreDictionary_create(
    CoreAllocatorRef allocator,
    CoreINT_U32 capacity,
    const CoreDictionaryKeyCallbacks * keyCallbacks,
    const CoreDictionaryValueCallbacks * valueCallbacks
)
{
    CoreDictionaryRef result = __CoreDictionary_init(
        allocator,
        capacity,
        keyCallbacks,
        valueCallbacks,
        true
    );
    
    CORE_DUMP_MSG(
        CORE_LOG_TRACE | CORE_LOG_INFO, 
        "->%s: new object %p\n", __FUNCTION__, result
    );
    
    return result;
}


/* CORE_PUBLIC */ CoreDictionaryRef
CoreDictionary_createImmutable(
    CoreAllocatorRef allocator,
    const void ** keys,
    const void ** values,
    CoreINT_U32 count,
    const CoreDictionaryKeyCallbacks * keyCallbacks,
    const CoreDictionaryValueCallbacks * valueCallbacks
)
{
    CoreDictionaryRef result = null;
    
    CORE_ASSERT_RET1(
        null,
        ((keys != null) && (values != null)) || (count == 0),
        CORE_LOG_ASSERT,
        "%s(): keys or values cannot be NULL when count %u is > 0",
        __PRETTY_FUNCTION__
    );
    
    result = __CoreDictionary_init(
        allocator,
        count,
        keyCallbacks,
        valueCallbacks,
        false
    );
    if (result != null)
    {
        // temporarily switch to mutable variant and add all the keys-values
        CoreINT_U32 idx;
        
        __CoreDictionary_setType(result, CORE_DICTIONARY_MUTABLE);
        for (idx = 0; idx < count; idx++)
        {
            CoreDictionary_addValue(result, keys[idx], values[idx]);
        }
        __CoreDictionary_setType(result, CORE_DICTIONARY_IMMUTABLE);
    }
    
    return result;
}


/* CORE_PUBLIC */ CoreDictionaryRef
CoreDictionary_createCopy(
    CoreAllocatorRef allocator,
    CoreImmutableDictionaryRef dictionary,
    CoreINT_U32 capacity
)
{
    CoreDictionaryRef result;
    const CoreDictionaryKeyCallbacks * keyCallbacks;
    const CoreDictionaryValueCallbacks * valueCallbacks;
    CoreINT_U32 count;

    CORE_IS_DICTIONARY_RET1(dictionary, null);
    
    count = CoreDictionary_getCount(dictionary);
    keyCallbacks = __CoreDictionary_getKeyCallbacks(dictionary);
    valueCallbacks = __CoreDictionary_getValueCallbacks(dictionary);
    
    result = __CoreDictionary_init(
        allocator, 
        capacity, 
        keyCallbacks,
        valueCallbacks,
        true
    );
    if ((result != null) && (count > 0))
    {
        CoreINT_U32 idx;
        const void * keybuf[64];
        const void * valuebuf[64];
        const void ** keys;
        const void ** values;
        
        keys = (count <= 64) ? keybuf : CoreAllocator_allocate(
            allocator,
            count * sizeof(const void *)
        );
        values = (count <= 64) ? valuebuf : CoreAllocator_allocate(
            allocator,
            count * sizeof(const void *)
        );
        
        CoreDictionary_copyKeysAndValues(dictionary, keys, values);
        if (capacity == 0)
        {
            __CoreDictionary_expand(result, count);
        }
        
        for (idx = 0; idx < count; idx++)
        {
            CoreDictionary_addValue(result, keys[idx], values[idx]);
        }       

        if (keys != keybuf)
        {
            CoreAllocator_deallocate(allocator, (void *) keys);
            CoreAllocator_deallocate(allocator, (void *) values);
        }
    }
    
    return result;    
}


/* CORE_PUBLIC */ CoreImmutableDictionaryRef
CoreDictionary_createImmutableCopy(
    CoreAllocatorRef allocator,
    CoreImmutableDictionaryRef dictionary
)
{
    CoreDictionaryRef result;
    const CoreDictionaryKeyCallbacks * keyCallbacks;
    const CoreDictionaryValueCallbacks * valueCallbacks;
    CoreINT_U32 count;

    CORE_IS_DICTIONARY_RET1(dictionary, null);
    
    count = CoreDictionary_getCount(dictionary);
    keyCallbacks = __CoreDictionary_getKeyCallbacks(dictionary);
    valueCallbacks = __CoreDictionary_getValueCallbacks(dictionary);

    result = __CoreDictionary_init(
        allocator, 
        count, 
        keyCallbacks,
        valueCallbacks,
        false
    );
    if ((result != null) && (count > 0))
    {
        CoreINT_U32 idx;
        const void * keybuf[64];
        const void * valuebuf[64];
        const void ** keys;
        const void ** values;
        
        keys = (count <= 64) ? keybuf : CoreAllocator_allocate(
            allocator,
            count * sizeof(const void *)
        );
        values = (count <= 64) ? valuebuf : CoreAllocator_allocate(
            allocator,
            count * sizeof(const void *)
        );
        
        CoreDictionary_copyKeysAndValues(dictionary, keys, values);

        __CoreDictionary_setType(result, CORE_DICTIONARY_MUTABLE);        
        for (idx = 0; idx < count; idx++)
        {
            CoreDictionary_addValue(result, keys[idx], values[idx]);
        }       
        __CoreDictionary_setType(result, CORE_DICTIONARY_IMMUTABLE);
        
        if (keys != keybuf)
        {
            CoreAllocator_deallocate(allocator, (void *) keys);
            CoreAllocator_deallocate(allocator, (void *) values);
        }
    }
    
    return (CoreImmutableDictionaryRef) result;    
}


/* CORE_PUBLIC */ CoreINT_U32
CoreDictionary_getCount(CoreImmutableDictionaryRef me)
{
    CORE_IS_DICTIONARY_RET1(me, 0);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    
    return me->count;
}


/* CORE_PUBLIC */ CoreINT_U32 
CoreDictionary_getCountOfValue(
    CoreImmutableDictionaryRef me,
    const void * value
)
{
    CoreINT_U32 result = 0;
    
    CORE_IS_DICTIONARY_RET1(me, 0);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);

    if (me->count > 0)
    {
        CoreINT_U32 idx;
        const CoreDictionaryValueCallbacks * valueCallbacks;
        CoreDictionary_equalCallback opEqual;
        
        valueCallbacks = __CoreDictionary_getValueCallbacks(me);
        opEqual = valueCallbacks->equal;
        
        for (idx = 0; idx < me->capacity; idx++)
        {
            if (IS_VALID(me, me->keys[idx]))
            {
                if ((value == me->values[idx])
                    || ((opEqual != null) && (opEqual(value, me->values[idx]))))
                {
                    result++;
                }
            }    
        }
    }
    
    return result;    
}


/* CORE_PUBLIC */ const void *
CoreDictionary_getValue(CoreImmutableDictionaryRef me, const void * key)
{
    const void * result = null;
    CoreINT_U32 idx = 0;

    CORE_IS_DICTIONARY_RET1(me, null);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
      
    if (me->count > 0)
    {
        idx = __CoreDictionary_getBucketForKey(me, key);
        if (idx != CORE_INDEX_NOT_FOUND)
        {
            result = me->values[idx];
        }
    }         
    
    return result;
}


/* CORE_PUBLIC */ CoreBOOL
CoreDictionary_getValueIfPresent(
    CoreImmutableDictionaryRef me,
    const void * key,
    const void ** value    
)
{
    CoreBOOL result = false;
    CoreINT_U32 idx;

    CORE_IS_DICTIONARY_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);

    if ((value != null) && (me->count > 0))
    {
        idx = __CoreDictionary_getBucketForKey(me, key);
        if (idx != CORE_INDEX_NOT_FOUND)
        {
            *value = me->values[idx];
            result = true;
        }
    }
    
    return result;    
}


/* CORE_PUBLIC */ CoreBOOL
CoreDictionary_containsKey(CoreImmutableDictionaryRef me, const void * key)
{
    CoreBOOL result = false;
    CoreINT_U32 idx;

    CORE_IS_DICTIONARY_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);

    if (me->count > 0)
    {
        idx = __CoreDictionary_getBucketForKey(me, key);
        if (idx != CORE_INDEX_NOT_FOUND)
        {
            result = true;
        }
    }         
    
    return result;
}


/* CORE_PUBLIC */ CoreBOOL
CoreDictionary_containsValue(CoreImmutableDictionaryRef me, const void * value)
{
    CORE_IS_DICTIONARY_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);

    return __CoreDictionary_containsValue(me, value);
}
        

/* CORE_PUBLIC */ void
CoreDictionary_copyKeysAndValues(
    CoreImmutableDictionaryRef me,
    const void ** keys,
    const void ** values
)
{
    CORE_IS_DICTIONARY_RET0(me);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET0(
        (keys != null) && (values != null),
        CORE_LOG_ASSERT,
        "%s(): keys %p or values %p is null!",
        __PRETTY_FUNCTION__
    );
    
    if (me->count > 0)
    {
        const void ** _keys;
        const void ** _values;
        CoreINT_U32 idx;
        CoreINT_U32 n;

        _keys = me->keys;
        _values = me->values;
        for (idx = 0, n = me->capacity; idx < n; idx++)
        {
            if (IS_VALID(me, _keys[idx]))
            {
                *keys++ = _keys[idx];
                *values++ = _values[idx];             
            }
        }
    }        
}


// CoreDictionary returns its keys and values on a rota basis (keys on even and
// values on odd positions). 
/* CORE_PROTECTED */ CoreINT_U32
_CoreDictionary_iterate(
    CoreImmutableDictionaryRef me, 
    __CoreIteratorState * state,
    const void ** buffer,
    CoreINT_U32 count
)
{
    CoreINT_U32 result = 0;
    
    state->items = buffer;
    if (me->count > 0)
    {
        const void ** keys;
        const void ** values;
        CoreINT_U32 idx = state->state;
        CoreINT_U32 n = me->capacity;

        keys = me->keys;
        values = me->values;
        for ( ; (idx < n) && (result < count); idx++)
        {
            if (IS_VALID(me, keys[idx]))
            {
                state->items[result++] = keys[idx];
                state->items[result++] = values[idx];             
            }
            state->state++;
        }
    } 
    
    return result;
}       


/* CORE_PUBLIC */ CoreBOOL
CoreDictionary_addValue(
    CoreDictionaryRef me, 
    const void * key,
    const void * value)
{
    CORE_IS_DICTIONARY_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreDictionary_getType(me) != CORE_DICTIONARY_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );

    return __CoreDictionary_addValue(me, key, value);
}


/* CORE_PUBLIC */ CoreBOOL
CoreDictionary_removeValue(
    CoreDictionaryRef me, 
    const void * key)
{
    CORE_IS_DICTIONARY_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreDictionary_getType(me) != CORE_DICTIONARY_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    
    return __CoreDictionary_removeValue(me, key);
}


/* CORE_PUBLIC */ CoreBOOL
CoreDictionary_replaceValue(
    CoreDictionaryRef me, 
    const void * key,
    const void * value)
{
    CORE_IS_DICTIONARY_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreDictionary_getType(me) != CORE_DICTIONARY_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    
    return __CoreDictionary_replaceValue(me, key, value);
}


/* CORE_PUBLIC */ void
CoreDictionary_clear(CoreDictionaryRef me)
{
    CORE_IS_DICTIONARY_RET0(me);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET0(
        (__CoreDictionary_getType(me) != CORE_DICTIONARY_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );

    __CoreDictionary_clear(me);
} 


/* CORE_PUBLIC */ void
CoreDictionary_applyFunction(
    CoreImmutableDictionaryRef me,
    CoreDictionaryApplyFunction map,
    void * context    
)
{
    CoreINT_U32 idx, n;
    
    CORE_IS_DICTIONARY_RET0(me);    
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET0(
        map != null,
        CORE_LOG_ASSERT,
        "%s(): apply function cannot be null!",
        __PRETTY_FUNCTION__
    );

    for (idx = 0, n = me->capacity; idx < n; idx++)
    {
        const void * key = me->keys[idx];
        if (IS_VALID(me, key))
        {
            map(key, me->values[idx], context);
        }
    }
}

