

/*****************************************************************************
 *
 * Includes
 * 
 *****************************************************************************/

#include <CoreFramework/CoreString.h>
#include "CoreInternal.h"
#include "CoreRuntime.h"
#include <stdlib.h>



/*****************************************************************************
 *
 * Types definitions
 * 
 ****************************************************************************/

#define CORE_STRING_BASE \
    CoreRuntimeObject core; \
    CoreINT_U32 length

#define CORE_STRING_BASE_IMMUTABLE \
    CORE_STRING_BASE; \
    CoreINT_U32 hash

struct __CoreString
{
    CORE_STRING_BASE;
};

typedef struct __CoreStringImmutable
{
    CORE_STRING_BASE_IMMUTABLE;
} __CoreStringImmutable;


// Now the real types...

typedef struct __CoreStringImmutable __StringImmutableInline;

typedef struct __StringImmutableExternal
{
    CORE_STRING_BASE_IMMUTABLE;   
    void * content;
    CoreAllocatorRef charactersDeallocator;
} __StringImmutableExternal;

typedef struct __StringMutableInline
{
    CORE_STRING_BASE_IMMUTABLE;
    CoreINT_U32 capacity;
    /* characters */
} __StringMutableInline;

typedef struct __StringMutableExternal
{
    CORE_STRING_BASE;
    void * content;
    CoreAllocatorRef charactersAllocator;
    CoreINT_U32 capacity;
} __StringMutableExternal;


typedef enum CoreStringType
{
    CORE_STRING_IMMUTABLE_INLINE      = 1,
    CORE_STRING_IMMUTABLE_EXTERNAL    = 2,
    CORE_STRING_MUTABLE_INLINE        = 3,
    CORE_STRING_MUTABLE_EXTERNAL      = 4,
    CORE_STRING_MUTABLE_STORAGE       = 5
} CoreStringType; 



static CoreClassID CoreStringID = CORE_CLASS_ID_UNKNOWN;

static __CoreStringImmutable __CORE_EMPTY_STRING = { { NULL, 0x0 }, 0, 0 };
CoreImmutableStringRef CORE_EMPTY_STRING = (CoreImmutableStringRef) &__CORE_EMPTY_STRING;



#define CORE_STRING_MINIMAL_CAPACITY      16UL
#define CORE_STRING_MAXIMAL_CAPACITY      (1 << 31)
#define CORE_STRING_MUTABLE_BUFFER_LIMIT  CORE_STRING_MAXIMAL_CAPACITY





#define CORE_IS_STRING(string) CORE_VALIDATE_OBJECT(string, CoreStringID)
#define CORE_IS_STRING_RET0(string) \
    do { if(!CORE_IS_STRING(string)) return ;} while (0)
#define CORE_IS_STRING_RET1(string, ret) \
    do { if(!CORE_IS_STRING(string)) return (ret);} while (0)



//
// Custom object's information:
//  - type of string in 0-2 bits
//  - has fixed length info on 3rd bit
//

#define CORE_STRING_TYPE_START            0
#define CORE_STRING_TYPE_LENGTH           3

#define CORE_STRING_IS_FIXED_START        3
#define CORE_STRING_IS_FIXED_LENGTH       1


CORE_INLINE CoreStringType
__CoreString_getType(CoreImmutableStringRef me)
{
    return (CoreStringType) CoreBitfield_getValue(
        ((const CoreRuntimeObject *) me)->info,
        CORE_STRING_TYPE_START,
        CORE_STRING_TYPE_LENGTH
    );
}

CORE_INLINE void
__CoreString_setType(CoreImmutableStringRef me, CoreStringType type)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) me)->info,
        CORE_STRING_TYPE_START,
        CORE_STRING_TYPE_LENGTH,
        (CoreINT_U32) type
    );
}

CORE_INLINE CoreBOOL
__CoreString_isFixed(CoreImmutableStringRef me)
{
    return (CoreBOOL) CoreBitfield_getValue(
        ((const CoreRuntimeObject *) me)->info,
        CORE_STRING_IS_FIXED_START,
        CORE_STRING_IS_FIXED_LENGTH
    );
}

CORE_INLINE void
__CoreString_setFixed(CoreImmutableStringRef me, CoreBOOL value)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) me)->info,
        CORE_STRING_IS_FIXED_START,
        CORE_STRING_IS_FIXED_LENGTH,
        (CoreINT_U32) value
    );
}


CORE_INLINE void *
__CoreString_getCharactersPtr(CoreImmutableStringRef me)
{
    void * result = null;
    
    switch (__CoreString_getType(me))
    {
        case CORE_STRING_IMMUTABLE_EXTERNAL:
            result = /*(CoreCHAR_8 *)*/ ((__StringImmutableExternal *) me)->content;
            break;
        case CORE_STRING_MUTABLE_EXTERNAL:
            result = /*(CoreCHAR_8 *)*/ ((__StringMutableExternal *) me)->content;
            break;
        case CORE_STRING_IMMUTABLE_INLINE:
            result = ((CoreCHAR_8 *) me) + sizeof(__StringImmutableInline);
            break;
        case CORE_STRING_MUTABLE_INLINE:
            result = ((CoreCHAR_8 *) me) + sizeof(__StringMutableInline);
            break;
    }
    
    return result;
}


CORE_INLINE CoreUniChar
__CoreString_getCharacterAtIndex(CoreImmutableStringRef me, CoreIndex index)
{
    CoreUniChar result = 0;
    
    switch (__CoreString_getType(me))
    {
        case CORE_STRING_IMMUTABLE_INLINE:
        case CORE_STRING_MUTABLE_INLINE:
        case CORE_STRING_IMMUTABLE_EXTERNAL:
        case CORE_STRING_MUTABLE_EXTERNAL:
            result = ((CoreUniChar *) __CoreString_getCharactersPtr(me))[index];
            break;
        case CORE_STRING_MUTABLE_STORAGE:
            break;
    }
    
    return result;
}


CORE_INLINE CoreINT_U32 
__CoreString_getLength(CoreImmutableStringRef me)
{
	return me->length;
}	


CORE_INLINE void 
__CoreString_setLength(CoreStringRef me, CoreINT_U32 newLength)
{
    me->length = newLength;
}


CORE_INLINE const void *
__CoreString_iterateConstCharacters(CoreImmutableStringRef me, CoreINT_U32 * pIter)
{
    const void * result = null;
    CoreINT_U32 length = __CoreString_getLength(me);
        
    if (*pIter <= length)
    {
        switch (__CoreString_getType(me))
        {
            case CORE_STRING_IMMUTABLE_INLINE:
            case CORE_STRING_MUTABLE_INLINE:
            case CORE_STRING_IMMUTABLE_EXTERNAL:
            case CORE_STRING_MUTABLE_EXTERNAL:
                result = __CoreString_getCharactersPtr(me);
                *pIter = length - *pIter;
                break;
            
            case CORE_STRING_MUTABLE_STORAGE:
                break;                    
        }
    }
    
    return result;
}


CORE_INLINE void
__CoreString_copyCharactersInRange(
    CoreImmutableStringRef me, 
    CoreRange range, 
    void * buffer
)
{
    switch (__CoreString_getType(me))
    {
        case CORE_STRING_IMMUTABLE_INLINE:
        case CORE_STRING_MUTABLE_INLINE:
        case CORE_STRING_IMMUTABLE_EXTERNAL:
        case CORE_STRING_MUTABLE_EXTERNAL:
        {
            const CoreUniChar * characters = __CoreString_getCharactersPtr(me);
            memmove(
                buffer,
                characters + range.offset,
                (size_t) range.length
            ); // memmove for sure... in case of someone calls this 
               // with my value as a buffer 
            break;
        case CORE_STRING_MUTABLE_STORAGE:
            break;                    
        }
    }
}


CORE_INLINE CoreINT_U32
__CoreString_getCapacity(CoreImmutableStringRef me)
{
    CoreINT_U32 result = 0;
    CoreStringType type = __CoreString_getType(me);
    
    switch (__CoreString_getType(me))
    {
        case CORE_STRING_IMMUTABLE_INLINE:
        case CORE_STRING_IMMUTABLE_EXTERNAL:
            result = me->length;
            break;
        case CORE_STRING_MUTABLE_INLINE:
            result = ((__StringMutableInline *) me)->capacity;
            break;
        case CORE_STRING_MUTABLE_EXTERNAL:
            result = ((__StringMutableExternal *) me)->capacity;
            break;
        case CORE_STRING_MUTABLE_STORAGE:
            break;
    }

    return result; 
}

// Call only on mutable objects.
CORE_INLINE void
__CoreString_setCapacity(CoreStringRef me, CoreINT_U32 newCapacity)
{
    switch (__CoreString_getType(me))
    {
        case CORE_STRING_MUTABLE_EXTERNAL:
            ((__StringMutableExternal *) me)->capacity = newCapacity;
            break;
        case CORE_STRING_MUTABLE_INLINE:
            ((__StringMutableInline *) me)->capacity = newCapacity;
            break;
    }    
}


// Call only on mutable objects with non-inline buffer.
CORE_INLINE void
__CoreString_setContentPtr(CoreStringRef me, const void * contentPtr)
{
    ((__StringMutableExternal *) me)->content = (void *) contentPtr;
}


static CoreHashCode 
__CoreString_hash(CoreObjectRef me)
{
    CoreINT_U32 result = 0;
    CoreImmutableStringRef _me = (CoreImmutableStringRef) me;
    CoreINT_U32 length;
    
    switch (__CoreString_getType(_me))
    {
        case CORE_STRING_IMMUTABLE_INLINE:
        case CORE_STRING_IMMUTABLE_EXTERNAL:
        {
            __CoreStringImmutable * im = (__CoreStringImmutable *) _me;
            if (im->hash > 0)
            {
                result = im->hash;
                break;
            }
        }
        default:
        {
            //return Core_hashCharacters(characters, length);
            length = __CoreString_getLength(_me);        
            if (length > 0)
            {
                const CoreCHAR_8 * /*CoreUniChar * */ chrs; 
                CoreINT_U32 idx, n;
                
                chrs = __CoreString_getCharactersPtr(_me);
                if (length < 64)
                {
                    CoreINT_U32 end4 = length & ~3;
                    
                    for (idx = 0; idx < end4; idx += 4)
                    {
                        result = result * 67503105 + chrs[idx + 0] * 16974593 + 
                                 chrs[idx + 1] * 66049 + chrs[idx + 2] * 257 + 
                                 chrs[idx + 3];
                    }
                    for ( ; idx < length; idx++)
                    {
                        result = result * 257 + chrs[idx];
                    }
                }
                else
                {
                    for (idx = 0, n = 16; idx < n; idx += 4)
                    {
                        result = result * 67503105 + chrs[idx + 0] * 16974593 + 
                                 chrs[idx + 1] * 66049 + chrs[idx + 2] * 257 + 
                                 chrs[idx + 3];
                    }
                    for (idx = (length >> 1) - 8, n = idx + 16; idx < n; idx += 4)
                    {
                        result = result * 67503105 + chrs[idx + 0] * 16974593 + 
                                 chrs[idx + 1] * 66049 + chrs[idx + 2] * 257 + 
                                 chrs[idx + 3];
                    }
                    for (idx = length - 16, n = idx + 16; idx < n; idx += 4)
                    {
                        result = result * 67503105 + chrs[idx + 0] * 16974593 + 
                                 chrs[idx + 1] * 66049 + chrs[idx + 2] * 257 + 
                                 chrs[idx + 3];
                    }
                }
            }
        
            result = result + (result << (length & 31));
            break;
        }
    }
    
    return result;    
}


static CoreBOOL
__CoreString_equal(CoreObjectRef me, CoreObjectRef to)
{
    CoreBOOL result = false;
    CORE_IS_STRING_RET1(me, false);
    CORE_IS_STRING_RET1(to, false);
    
    if (me == to)
    {
        result = true;
    }
    else
    {
        // Some optimization checks before calling a comprehensive compare
        // function...
        CoreImmutableStringRef _me = (CoreImmutableStringRef) me;
        CoreImmutableStringRef _to = (CoreImmutableStringRef) to;
        CoreINT_U32 meLength = CoreString_getLength(_me);
        CoreINT_U32 toLength = CoreString_getLength(_to);
        
        if (meLength == toLength)
        {
            if (meLength == 0)
            {
                result = true;
            }
            else
            {
                result = (CoreString_compare(_me, _to) == CORE_COMPARISON_EQUAL)
                    ? true : false;
            }
        }
    }
    
    return result;
}


CORE_INLINE CoreINT_U32
__CoreString_roundUpCapacity(CoreINT_U32 capacity)
{
    return (capacity <= CORE_STRING_MINIMAL_CAPACITY)
        ? CORE_STRING_MINIMAL_CAPACITY
        : (capacity < 1024)
            ? (1 << (CoreINT_U32)(CoreBits_mostSignificantBit(capacity - 1) + 1))
            : (CoreINT_U32)((capacity * 3) / 2);
}


static CoreImmutableStringRef
__CoreString_getCopyOfDescription(CoreObjectRef me)
{
    return null;
}


static void
__CoreString_cleanup(CoreObjectRef me)
{
    CoreImmutableStringRef _me = (CoreImmutableStringRef) me;
    CoreAllocatorRef allocator = null;
    void * memory = null;
    
    switch (__CoreString_getType(me))
    {
        case CORE_STRING_IMMUTABLE_EXTERNAL:
        {
            allocator = ((__StringImmutableExternal *) _me)->charactersDeallocator;
            memory = ((__StringImmutableExternal *) _me)->content;
            break;
        }
        case CORE_STRING_MUTABLE_EXTERNAL:
        {
            __StringMutableExternal * __me = (__StringMutableExternal *) me;
            allocator = (__me->charactersAllocator != null)
                ? __me->charactersAllocator
                : Core_getAllocator(me);
            memory = __me->content;
            break;
        }
    }
    
    if (allocator != null)
    {
        CoreAllocator_deallocate(allocator, memory);
        Core_release(allocator);
    }
}


/*
 * Capable to expand only non-fixed (external) mutable variants. 
 * This must be ensured by a caller. 
 * Currently STORAGE variant is not available.
 */   
static CoreBOOL
_CoreString_expand(CoreStringRef me, CoreINT_U32 needed)
{
    CoreBOOL result = false;
    CoreINT_U32 neededCapacity = me->length + needed;
    
    if (neededCapacity < CORE_STRING_MUTABLE_BUFFER_LIMIT)
    {
        __StringMutableExternal * _me = (__StringMutableExternal *) me;
        CoreINT_U32 capacity = __CoreString_roundUpCapacity(neededCapacity);
        CoreAllocatorRef allocator;
        
        allocator = (_me->charactersAllocator == null) 
            ? Core_getAllocator(me)
            : _me->charactersAllocator;
        if (_me->content == null)
        {
            _me->content = CoreAllocator_allocate(
                allocator,
                capacity * sizeof(CoreUniChar)
            );
        }
        else
        {
            _me->content = CoreAllocator_reallocate(
                allocator,
                _me->content,
                capacity * sizeof(CoreUniChar)
            );        
        }
        result = (_me->content != null) ? true : false;
        if (result)
        {
            _me->capacity = capacity;
        }
    }
    
    return result;
}


CORE_INLINE CoreBOOL
__CoreString_accomodateLength(CoreStringRef me, CoreINT_U32 newLength)
{
    CoreBOOL result = true;
    CoreINT_U32 length = __CoreString_getLength(me);
    CoreINT_U32 oldCapacity = __CoreString_getCapacity(me);
    
    switch (__CoreString_getType(me))
    {
        case CORE_STRING_MUTABLE_EXTERNAL:
        case CORE_STRING_MUTABLE_STORAGE:
        {
            if (__CoreString_isFixed(me))
            {
                result = (newLength <= oldCapacity) ? true : false; 
                CORE_ASSERT(
                    result,
                    CORE_LOG_ASSERT,
                    "%s(): length %u is over the maximal fixed capacity %u",
                    __PRETTY_FUNCTION__, newLength, oldCapacity
                );
            }
            else 
            {
                if (length < newLength)
                {
                    result = _CoreString_expand(me, newLength - length);
                }
            }
            break;
        }
    }
    
    if (result)
    {
        /*
         * Question: should we reset characters only when newLength is greater
         * than string's capacity?
         */                  
        if (length < newLength)
        //if (newLength > oldCapacity)
        {
            memset(
                (void *) ((CoreUniChar *) __CoreString_getCharactersPtr(me) + length),
                0,
                newLength - length
            );
        }
        __CoreString_setLength(me, newLength);    
    }
    
    return result;
}


/* does no range check... must be ensured by caller */
static CoreBOOL
_CoreString_replaceASCIIInRange(
    CoreStringRef me, 
    CoreRange range,
    const CoreCHAR_8 * characters, 
    CoreINT_U32 length
)
{
    CoreBOOL result = false;
    CoreINT_S32 change = (CoreINT_S32) length - range.length;
    CoreINT_U32 newLength = (CoreINT_U32) ((CoreINT_S32) me->length + change);
    const CoreCHAR_8 * value;

    //
    // Currently only for inlined and external mutable buffer.
    // Note that newLength can never be < 0.
    //
    switch (__CoreString_getType(me))
    {
        case CORE_STRING_MUTABLE_INLINE:
        {
            __StringMutableInline * _me = (__StringMutableInline *) me;
            if (_me->capacity >= newLength)
            {
                result = true;
            }
            break;
        }
        case CORE_STRING_MUTABLE_EXTERNAL:
        {
            __StringMutableExternal * _me = (__StringMutableExternal *) me;
            result = true;
            if ((_me->content == NULL) || (_me->capacity < newLength))
            {
                result = _CoreString_expand(me, (CoreINT_U32) change);
            }
            break;
        }
    }
    
    if (result)
    {
        value = (const CoreCHAR_8 *) __CoreString_getCharactersPtr(me);
        
        //
        // If needed, rearrange characters in my own value.
        //
        if ((change != 0) && (range.offset + range.length < me->length))
        { 
            memmove(
                (void *) (value + range.offset + length),
                (const void *) (value + range.offset + range.length),
                (size_t) ((me->length - range.offset - range.length) 
                            * sizeof(CoreCHAR_8))
            );
        }
        
        //
        // When the length is positive, copy characters.
        //
        if (length > 0) 
        {
            memmove(
                (void *) (value + range.offset),
                (const void *) characters,
                (size_t) (length * sizeof(CoreCHAR_8))
            ); // memmove for sure... in case of someone calls this 
               // with my value as characters
        }

        // Update the length.
        me->length = newLength;
    }
	
	return result;
}



static const CoreClass __CoreStringClass =
{
    0x00,                           // version
    "CoreString",                     // name
    NULL,                           // init
    NULL,                           // copy
    __CoreString_cleanup,             // cleanup
    __CoreString_equal,               // equal
    __CoreString_hash,                // hash
    __CoreString_getCopyOfDescription // getCopyOfDescription
};

/* CORE_PROTECTED */ void
CoreString_initialize(void)
{
    CoreStringID = CoreRuntime_registerClass(&__CoreStringClass);
}

/* CORE_PUBLIC */ CoreClassID
CoreString_getClassID(void)
{
    return CoreStringID;
}



static CoreStringRef
__CoreString_init(
    CoreAllocatorRef allocator,
	CoreBOOL isMutable,
	const void * characters,
	CoreINT_U32 length, // length of characters
	CoreINT_U32 maxCapacity,
	CoreAllocatorRef charactersAllocator
)
{
    struct __CoreString * result = null;
    CoreINT_U32 size = 0;
    CoreBOOL isFixed = false;
    CoreStringType type;

    if (isMutable)
    {
        isFixed = (maxCapacity != 0) ? true : false;
        if (isFixed && (charactersAllocator == null))
        {
            type = CORE_STRING_MUTABLE_INLINE;
            size += sizeof(__StringMutableInline) +
                    maxCapacity * sizeof(CoreCHAR_8);
        }
        else
        {
            type = CORE_STRING_MUTABLE_EXTERNAL;
            size += sizeof(__StringMutableExternal);
        }
    }
    else
    {
        if (charactersAllocator == null)
        {
            type = CORE_STRING_IMMUTABLE_INLINE;
            size += sizeof(__StringImmutableInline) + 
                    maxCapacity * sizeof(CoreCHAR_8);
        }
        else
        {
            type = CORE_STRING_IMMUTABLE_EXTERNAL;
            size += sizeof(__StringImmutableExternal);
        }
    }
        
    result = (struct __CoreString *) CoreRuntime_createObject(
        allocator, 
        CoreStringID, 
        size
    );
    if (result != null)
    {
        __CoreString_setType(result, type);
        __CoreString_setFixed(result, isFixed);
        
        switch (type)
        {
            case CORE_STRING_IMMUTABLE_INLINE:
            {
                __StringImmutableInline * me;
                
                me = (__StringImmutableInline *) result;
                me->length = length;
                memcpy(
                    (CoreCHAR_8 *) me + sizeof(__StringImmutableInline), 
                    characters, 
                    me->length * sizeof(CoreCHAR_8)
                );
                me->hash = 0;
                break;
            }
            case CORE_STRING_IMMUTABLE_EXTERNAL:
            {
                __StringImmutableExternal * me;
                
                me = (__StringImmutableExternal *) result; 
                me->length = length;
                me->hash = 0;
                me->content = (void *) characters;
                me->charactersDeallocator = Core_retain(charactersAllocator);
                break;
            }
            case CORE_STRING_MUTABLE_INLINE:
            {
                __StringMutableInline * me = (__StringMutableInline *) result;
                me->length = 0;
                me->capacity = maxCapacity;                    
                break;
            }
            case CORE_STRING_MUTABLE_EXTERNAL:
            {
                __StringMutableExternal * me = (__StringMutableExternal *) result;
                me->length = 0;
                me->capacity = maxCapacity;
                me->content = null;
                me->charactersAllocator = charactersAllocator;
                if (charactersAllocator != null)
                {
                    Core_retain(me->charactersAllocator);                    
                }
                break;
            }
        } 
    }
    else
    {
        if (type == CORE_STRING_MUTABLE_INLINE)
        {
            //
            // Allocation was not successful (most probably the requested
            // size is too large). So try another attempt with STORAGE.
            //
            result = __CoreString_init(
                allocator,
                true,
                characters,
                length,
                0, // temporarily set to 0
                null
            );
        }
    }
    
    return (CoreStringRef) result;    
}


/* CORE_PUBLIC */ CoreStringRef
CoreString_create(
    CoreAllocatorRef allocator,
    CoreINT_U32 maxCapacity
)
{
    return __CoreString_init(
        allocator,
        true,
        null,
        0,
        maxCapacity,
        null
    );        
}


/* CORE_PUBLIC */ CoreImmutableStringRef
CoreString_createImmutable(
    CoreAllocatorRef allocator,
    const CoreUniChar * characters,
    CoreINT_U32 length
)
{
    return __CoreString_init(
        allocator,
        false,
        characters,
        length,
        length,
        null
    );        
}


/* CORE_PUBLIC */ CoreImmutableStringRef
CoreString_createImmutableWithASCII(
    CoreAllocatorRef allocator,
    const CoreCHAR_8 * characters,
    CoreINT_U32 length
)
{
    CoreImmutableStringRef result = __CoreString_init(
        allocator,
        false,
        characters,
        length,
        length,
        null
    );        
    
    CORE_DUMP_MSG(
        CORE_LOG_TRACE | CORE_LOG_INFO, 
        "->%s: new object %p\n", __FUNCTION__, result
    );

    return result;
}

/* CORE_PUBLIC */ CoreImmutableStringRef
CoreString_createImmutableCopy(
    CoreAllocatorRef allocator,
    CoreImmutableStringRef str
)
{
    CoreImmutableStringRef result = null;
    CoreStringType type = __CoreString_getType(str);
    
    CORE_ASSERT_RET1(
        null,
        str != NULL,
        CORE_LOG_ASSERT,
        "%s(): cannot create copy of null object", __PRETTY_FUNCTION__
    );
        
    if ((type == CORE_STRING_IMMUTABLE_INLINE) ||
        (type == CORE_STRING_IMMUTABLE_EXTERNAL))
    {
        result = (CoreImmutableStringRef) Core_retain(str);
    }
    else
    {
        CoreINT_U32 length = CoreString_getLength(str);
        result = __CoreString_init(
            allocator,
            false,
            CoreString_getConstASCIICharactersPtr(str), // TODO: getConstCharactersPtr()
            length,
            length,
            null        
        );
    }
    
    return result;
} 

/* CORE_PUBLIC */ CoreStringRef
CoreString_createWithExternalCharactersNoCopy(
    CoreAllocatorRef allocator,
    const CoreUniChar * characters,
    CoreINT_U32 length,
    CoreINT_U32 capacity,
    CoreAllocatorRef charactersAllocator
)
{
    CORE_ASSERT_RET1(
        null,
        (characters != null) || (length == 0),
        CORE_LOG_ASSERT,
        "%s(): characters cannot be NULL when length is > 0",
        __PRETTY_FUNCTION__
    );

    return __CoreString_init(
        allocator,
        true,
        characters,
        length,
        capacity,
        (charactersAllocator == null)
            ? CoreAllocator_getDefault() : charactersAllocator
    );
}


/* CORE_PUBLIC */ CoreImmutableStringRef
CoreString_createImmutableWithCharactersNoCopy(
    CoreAllocatorRef allocator,
    const CoreUniChar * characters,
    CoreINT_U32 length,
    CoreAllocatorRef charactersDeallocator
)
{
    CORE_ASSERT_RET1(
        null,
        (characters != null) || (length == 0),
        CORE_LOG_ASSERT,
        "%s(): characters cannot be NULL when length is > 0",
        __PRETTY_FUNCTION__
    );
            
    return __CoreString_init(
        allocator,
        false,
        characters,
        length,
        length,
        (charactersDeallocator == null) 
            ? CoreAllocator_getDefault() : charactersDeallocator
    );        
}


/* CORE_PUBLIC */ /*CoreStringRef
CoreString_createCopy(
    CoreAllocatorRef allocator,
    CoreINT_U32 maxCapacity,
    CoreImmutableStringRef string
)
{
    CoreINT_U32 length = CoreString_getLength(string);
    
    CORE_ASSERT_RET1(
        null,
        (maxCapacity == 0) || (maxCapacity >= length), 
        CORE_LOG_ASSERT,
        "%s(): maxCapacity cannot be less than string's length",
        __PRETTY_FUNCTION__
    );
            
    return __CoreString_init(
        allocator,
        true,
        CoreString_getCharactersPtr(string),
        length,
        maxCapacity,
        null        
    );
}*/


/* CORE_PUBLIC */ /*CoreImmutableStringRef
CoreString_createImmutableCopy(
    CoreAllocatorRef allocator,
    CoreImmutableStringRef string
)
{
    CoreImmutableStringRef result = null;
    CoreStringType type = __CoreString_getType(string);
    
    CORE_ASSERT_RET1(
        null,
        string != NULL,
        CORE_LOG_ASSERT,
        "%s(): cannot create copy of null object", __PRETTY_FUNCTION__
    );
        
    if ((type == CORE_STRING_IMMUTABLE_INLINE) ||
        (type == CORE_STRING_IMMUTABLE_EXTERNAL))
    {
        result = (CoreImmutableStringRef) Core_retain(string);
    }
    else
    {
        CoreINT_U32 length = CoreString_getLength(string);
        result = __CoreString_init(
            allocator,
            false,
            CoreString_getCharactersPtr(string),
            length,
            length,
            null        
        );
    }
    
    return result;
}*/    


/* CORE_PUBLIC */ CoreINT_U32
CoreString_getLength(CoreImmutableStringRef me)
{
    CORE_IS_STRING_RET1(me, 0);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    
    return __CoreString_getLength(me);
}


/* CORE_PUBLIC */ CoreUniChar
CoreString_getCharacterAtIndex(CoreImmutableStringRef me, CoreINT_U32 index)
{
    CORE_IS_STRING_RET1(me, 0);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    
    return __CoreString_getCharacterAtIndex(me, index);
}


/* CORE_PUBLIC */ const CoreUniChar *
CoreString_getConstCharactersPtr(CoreImmutableStringRef me)
{
    CORE_IS_STRING_RET1(me, null);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    
    return __CoreString_getCharactersPtr(me);
}


/* CORE_PUBLIC */ const CoreCHAR_8 *
CoreString_getConstASCIICharactersPtr(CoreImmutableStringRef me)
{
    CORE_IS_STRING_RET1(me, null);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    
    return (const CoreCHAR_8 *) __CoreString_getCharactersPtr(me);
}


/* CORE_PUBLIC */ /*CoreUniChar *
CoreString_getCharactersPtr(CoreImmutableStringRef me)
{
    CORE_IS_STRING_RET1(me, null);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        null,
        (__CoreString_getType(me) != CORE_STRING_IMMUTABLE_INLINE) &&
        (__CoreString_getType(me) != CORE_STRING_IMMUTABLE_EXTERNAL),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    
    return (CoreUniChar *) __CoreString_getCharactersPtr(me);
}*/


/* CORE_PUBLIC */ const CoreUniChar *
CoreString_iterateConstCharacters(
    CoreImmutableStringRef me, 
    CoreINT_U32 * pIter
)
{
    CORE_IS_STRING_RET1(me, null);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        null,
        pIter != null,
        CORE_LOG_ASSERT,
        "%s(): pIter cannot be null-pointer", __PRETTY_FUNCTION__
    );
    
    return __CoreString_iterateConstCharacters(me, pIter);
}


/* CORE_PUBLIC */ CoreUniChar *
CoreString_iterateCharacters(CoreImmutableStringRef me, CoreINT_U32 * pIter)
{
    CORE_IS_STRING_RET1(me, null);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        null,
        (__CoreString_getType(me) != CORE_STRING_IMMUTABLE_INLINE) &&
        (__CoreString_getType(me) != CORE_STRING_IMMUTABLE_EXTERNAL),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET1(
        null,
        pIter != null,
        CORE_LOG_ASSERT,
        "%s(): pIter cannot be null-pointer", __PRETTY_FUNCTION__
    );
    
    return (void *) __CoreString_iterateConstCharacters(me, pIter);
}


/* CORE_PUBLIC */ void
CoreString_copyCharacters(CoreImmutableStringRef me, CoreUniChar * buffer)
{
    CORE_IS_STRING_RET0(me);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET0(
        (buffer != null),
        CORE_LOG_ASSERT,
        "%s(): buffer cannot be null-pointer", __PRETTY_FUNCTION__
    );

    __CoreString_copyCharactersInRange(
        me, 
        CoreRange_create(0, __CoreString_getLength(me)),
        buffer
    );
}


/* CORE_PUBLIC */ CoreBOOL
CoreString_copyCharactersInRange(
    CoreImmutableStringRef me, 
    CoreRange range, 
    CoreUniChar * buffer
)
{
    CORE_IS_STRING_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        range.offset + range.length <= __CoreString_getLength(me),
        CORE_LOG_ASSERT,
        "%s(): parameter range out of bounds", __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET1(
        false,
        (buffer != null) || (range.length == 0),
        CORE_LOG_ASSERT,
        "%s(): buffer cannot be null-pointer when range.length is %u > 0", 
        __PRETTY_FUNCTION__, range.length
    );
    
    __CoreString_copyCharactersInRange(me, range, buffer);
    
    return true;
}


/* CORE_PUBLIC */ CoreComparison
CoreString_compare(CoreImmutableStringRef me, CoreImmutableStringRef to)
{
    CoreComparison result = CORE_COMPARISON_UNCOMPARABLE;

    CORE_IS_STRING_RET1(me, CORE_COMPARISON_UNCOMPARABLE);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        CORE_COMPARISON_UNCOMPARABLE,
        (to != null),
        CORE_LOG_ASSERT,
        "%s(): object to compare cannot be null!",
        __PRETTY_FUNCTION__
    );
    
        
    if (me == to)
    {
        result = CORE_COMPARISON_EQUAL;
    }
    else
    {
        const void * meBuffer = __CoreString_getCharactersPtr(me);
        const void * toBuffer = __CoreString_getCharactersPtr(to);
        
        if ((meBuffer != null) && (toBuffer != null))
        {
            CoreINT_U32 meLength = __CoreString_getLength(me);
            CoreINT_U32 toLength = __CoreString_getLength(to);
            CoreINT_U32 cmpLength = min(meLength, toLength);
            if (cmpLength > 0)
            {
                int res = memcmp(meBuffer, toBuffer, cmpLength);
                result = (res < 0)
                            ? CORE_COMPARISON_LESS_THAN
                            : (res > 0)
                                ? CORE_COMPARISON_GREATER_THAN
                                : CORE_COMPARISON_EQUAL;
            }
            else
            {
                // special case for 0 length
                result = (meLength < toLength)
                            ? CORE_COMPARISON_LESS_THAN
                            : (meLength > toLength)
                                ? CORE_COMPARISON_GREATER_THAN
                                : CORE_COMPARISON_EQUAL;
            }
        }
    }
    
    return result;
}


/* CORE_PUBLIC */ CoreBOOL
CoreString_setLength(CoreStringRef me, CoreINT_U32 newLength)
{
    CORE_IS_STRING_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreString_getType(me) != CORE_STRING_IMMUTABLE_INLINE) &&
        (__CoreString_getType(me) != CORE_STRING_IMMUTABLE_EXTERNAL),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );

    return __CoreString_accomodateLength(me, newLength);        
}


/* CORE_PUBLIC */ void
CoreString_setExternalCharactersNoCopy(
    CoreStringRef me,
    const CoreUniChar * characters,
    CoreINT_U32 length,
    CoreINT_U32 capacity 
)
{
    CORE_IS_STRING_RET0(me);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET0(
        (__CoreString_getType(me) == CORE_STRING_MUTABLE_EXTERNAL),
        CORE_LOG_ASSERT,
        "%s(): the string object must have been created by "
        "createWithExternalCharactersNoCopy constructor!",
        __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET0(
        (length <= capacity) && ((characters != null) || (capacity == 0)),
        CORE_LOG_ASSERT,
        "%s(): invalid args: characters %p, length %u, capacity %u",
        __PRETTY_FUNCTION__, characters, length, capacity
    );

    __CoreString_setContentPtr(me, characters);
    __CoreString_setLength(me, length);
    __CoreString_setCapacity(me, capacity);    
}


/* CORE_PUBLIC */ void
CoreString_clear(CoreStringRef me)
{
    (void) CoreString_setLength(me, 0);
}


/* CORE_PUBLIC */ CoreBOOL
CoreString_remove(CoreStringRef me, CoreRange range)
{
    CORE_IS_STRING_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreString_getType(me) != CORE_STRING_IMMUTABLE_INLINE) &&
        (__CoreString_getType(me) != CORE_STRING_IMMUTABLE_EXTERNAL),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET1(
        false,
        range.offset + range.length <= __CoreString_getLength(me),
        CORE_LOG_ASSERT,
        "%s(): parameter range out of bounds", __PRETTY_FUNCTION__
    );
        
    return false;//_CoreString_replaceCharactersInRange(me, range, null, 0);    
}


/* CORE_PUBLIC */ CoreBOOL
CoreString_appendCharacters(
    CoreStringRef me, 
    const CoreUniChar * characters, 
    CoreINT_U32 length
)
{
    CORE_IS_STRING_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreString_getType(me) != CORE_STRING_IMMUTABLE_INLINE) &&
        (__CoreString_getType(me) != CORE_STRING_IMMUTABLE_EXTERNAL),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET1(
        false,
        (characters != null) || (length == 0),
        CORE_LOG_ASSERT,
        "%s(): characters cannot be null-pointer if length is %u > 0", 
        __PRETTY_FUNCTION__, length
    );
    
    return false; /*_CoreString_replaceCharactersInRange(
        me, 
        CoreRange_create(__CoreString_getLength(me), 0),
        characters, 
        length
    );*/    
}


/* CORE_PUBLIC */ CoreBOOL
CoreString_appendASCIICharacters(
    CoreStringRef me, 
    const CoreCHAR_8 * characters, 
    CoreINT_U32 length
)
{
    CORE_IS_STRING_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreString_getType(me) != CORE_STRING_IMMUTABLE_INLINE) &&
        (__CoreString_getType(me) != CORE_STRING_IMMUTABLE_EXTERNAL),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET1(
        false,
        (characters != null) || (length == 0),
        CORE_LOG_ASSERT,
        "%s(): characters cannot be null-pointer if length is %u > 0", 
        __PRETTY_FUNCTION__, length
    );
    
    return _CoreString_replaceASCIIInRange(
        me, 
        CoreRange_create(__CoreString_getLength(me), 0),
        characters, 
        length
    );    
}


/* CORE_PUBLIC */ CoreBOOL
CoreString_append(
    CoreStringRef me, 
    CoreImmutableStringRef appendedString
)
{
    CORE_IS_STRING_RET1(me, false);
    CORE_IS_STRING_RET1(appendedString, false);
    CORE_ASSERT_RET1(
        false,
        (__CoreString_getType(me) != CORE_STRING_IMMUTABLE_INLINE) &&
        (__CoreString_getType(me) != CORE_STRING_IMMUTABLE_EXTERNAL),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );

    return false;
}


/* CORE_PUBLIC */ /*CoreBOOL
CoreString_insert(
    CoreStringRef me, 
    CoreINT_U32 index,
    CoreImmutableStringRef insertedString
)
{
    CORE_IS_STRING_RET1(me, false);
    CORE_IS_STRING_RET1(insertedString, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreString_getType(me) != CORE_STRING_IMMUTABLE_INLINE) &&
        (__CoreString_getType(me) != CORE_STRING_IMMUTABLE_EXTERNAL),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET1(
        false,
        (index <= __CoreString_getLength(me)),
        CORE_LOG_ASSERT,
        "%s(): index %u is out of bounds", 
        __PRETTY_FUNCTION__, length
    );
    
    return //_CoreString_replaceCharactersInRange(
        me, 
        CoreRange_create(index, 0),
        characters, 
        length
    );    
}*/


/* CORE_PUBLIC */ /*CoreBOOL
CoreString_replaceCharactersInRange(
    CoreStringRef me, 
    CoreRange range,
	const void * characters, 
    CoreINT_U32 length
)
{
    CORE_IS_STRING_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreString_getType(me) != CORE_STRING_IMMUTABLE_INLINE) &&
        (__CoreString_getType(me) != CORE_STRING_IMMUTABLE_EXTERNAL),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET1(
        false,
        range.offset + range.length <= __CoreString_getLength(me),
        CORE_LOG_ASSERT,
        "%s(): parameter range out of bounds", __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET1(
        false,
        (characters != null) || (length == 0),
        CORE_LOG_ASSERT,
        "%s(): characters cannot be null-pointer if length is %u > 0", 
        __PRETTY_FUNCTION__, length
    );

    return _CoreString_replaceCharactersInRange(me, range, characters, length);    
}*/


/* CORE_PUBLIC */ CoreBOOL
CoreString_replace(
    CoreStringRef me, 
    CoreRange range,
    CoreImmutableStringRef replacement
)
{
    CORE_IS_STRING_RET1(me, false);
    CORE_IS_STRING_RET1(replacement, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreString_getType(me) != CORE_STRING_IMMUTABLE_INLINE) &&
        (__CoreString_getType(me) != CORE_STRING_IMMUTABLE_EXTERNAL),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET1(
        false,
        range.offset + range.length <= __CoreString_getLength(me),
        CORE_LOG_ASSERT,
        "%s(): parameter range out of bounds", __PRETTY_FUNCTION__
    );

    return false; //_CoreString_replaceCharactersInRange(me, range, characters, length);    
}

