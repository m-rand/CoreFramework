

/*****************************************************************************
 *
 * Includes
 * 
 *****************************************************************************/

#include "CoreData.h"
#include "CoreString.h"
#include "CoreInternal.h"
#include "CoreRuntime.h"
#include <stdlib.h>



/*****************************************************************************
 *
 * Types definitions
 * 
 ****************************************************************************/

// A generic CoreData object... 
struct __CoreData
{
    CoreRuntimeObject _core;
    CoreINT_U32 _length;
    CoreINT_U32 _capacity;
    CoreAllocatorRef _bytesDeallocator;
    void * _content;
};


typedef enum CoreDataType
{
    CORE_DATA_IMMUTABLE         = 1,
    CORE_DATA_MUTABLE           = 2,
    CORE_DATA_MUTABLE_STORAGE   = 3
} CoreDataType; 



static CoreClassID CoreDataID = CORE_CLASS_ID_UNKNOWN;



#define CORE_DATA_MINIMAL_CAPACITY      16UL
#define CORE_DATA_MAXIMAL_CAPACITY      (1 << 31)
#define CORE_DATA_MUTABLE_BUFFER_LIMIT  CORE_DATA_MAXIMAL_CAPACITY
#define CORE_DATA_STORAGE_SWITCH        CORE_DATA_MAXIMAL_CAPACITY




#define CORE_IS_DATA(data) CORE_VALIDATE_OBJECT(data, CoreDataID)
#define CORE_IS_DATA_RET0(data) \
    do { if(!CORE_IS_DATA(data)) return ;} while (0)
#define CORE_IS_DATA_RET1(data, ret) \
    do { if(!CORE_IS_DATA(data)) return (ret);} while (0)



//
// Custom object's information:
//  - type of data in 0-1 bits
//  - has fixed length info on 3rd bit
//

#define CORE_DATA_TYPE_START            0
#define CORE_DATA_TYPE_LENGTH           2

#define CORE_DATA_IS_MUTABLE_BIT        2
#define CORE_DATA_IS_FIXED_BIT          3
#define CORE_DATA_IS_INLINE_BIT         4



CORE_INLINE CoreDataType
__CoreData_getType(CoreImmutableDataRef me)
{
    return (CoreDataType) CoreBitfield_getValue(
        ((const CoreRuntimeObject *) me)->info,
        CORE_DATA_TYPE_START,
        CORE_DATA_TYPE_LENGTH
    );
}

CORE_INLINE void
__CoreData_setType(CoreImmutableDataRef me, CoreDataType type)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) me)->info,
        CORE_DATA_TYPE_START,
        CORE_DATA_TYPE_LENGTH,
        (CoreINT_U32) type
    );
}

CORE_INLINE CoreBOOL
__CoreData_isMutable(CoreImmutableDataRef me)
{
    return (CoreBOOL) CoreBitfield_getValue(
        ((const CoreRuntimeObject *) me)->info,
        CORE_DATA_IS_MUTABLE_BIT,
        1
    );
}

CORE_INLINE void
__CoreData_setMutable(CoreImmutableDataRef me, CoreBOOL value)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) me)->info,
        CORE_DATA_IS_MUTABLE_BIT,
        1,
        (CoreINT_U32) value
    );
}

CORE_INLINE CoreBOOL
__CoreData_isFixed(CoreImmutableDataRef me)
{
    return (CoreBOOL) CoreBitfield_getValue(
        ((const CoreRuntimeObject *) me)->info,
        CORE_DATA_IS_FIXED_BIT,
        1
    );
}

CORE_INLINE void
__CoreData_setFixed(CoreImmutableDataRef me, CoreBOOL value)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) me)->info,
        CORE_DATA_IS_FIXED_BIT,
        1,
        (CoreINT_U32) value
    );
}

CORE_INLINE CoreBOOL
__CoreData_isInline(CoreImmutableDataRef me)
{
    return (CoreBOOL) CoreBitfield_getValue(
        ((const CoreRuntimeObject *) me)->info,
        CORE_DATA_IS_INLINE_BIT,
        1
    );
}

CORE_INLINE void
__CoreData_setInline(CoreImmutableDataRef me, CoreBOOL value)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) me)->info,
        CORE_DATA_IS_INLINE_BIT,
        1,
        (CoreINT_U32) value
    );
}


// used only for non-storage varietes
CORE_INLINE CoreINT_U8 *
__CoreData_getBytesPtr(CoreImmutableDataRef me)
{
    return (CoreINT_U8 *) me->_content;
}


CORE_INLINE CoreINT_U8
__CoreData_getByteAtIndex(CoreImmutableDataRef me, CoreIndex index)
{
    CoreINT_U8 result = 0;
    
    switch (__CoreData_getType(me))
    {
        case CORE_DATA_IMMUTABLE:
        case CORE_DATA_MUTABLE:
            result = __CoreData_getBytesPtr(me)[index];
            break;
        case CORE_DATA_MUTABLE_STORAGE:
            break;
    }
    
    return result;
}


CORE_INLINE CoreINT_U32 
__CoreData_getLength(CoreImmutableDataRef me)
{
	return me->_length;
}	


CORE_INLINE void 
__CoreData_setLength(CoreDataRef me, CoreINT_U32 newLength)
{
    me->_length = newLength;
}


CORE_INLINE const CoreINT_U8 *
__CoreData_iterateConstBytes(CoreImmutableDataRef me, CoreINT_U32 * pIter)
{
    const void * result = null;
    CoreINT_U32 length = __CoreData_getLength(me);
        
    if (*pIter <= length)
    {
        switch (__CoreData_getType(me))
        {
            case CORE_DATA_IMMUTABLE:
            case CORE_DATA_MUTABLE:
                result = __CoreData_getBytesPtr(me);
                *pIter = length - *pIter;
                break;
            
            case CORE_DATA_MUTABLE_STORAGE:
                break;                    
        }
    }
    
    return (const CoreINT_U8 *) result;
}


CORE_INLINE void
__CoreData_copyBytesInRange(
    CoreImmutableDataRef me, 
    CoreRange range, 
    void * buffer
)
{
    switch (__CoreData_getType(me))
    {
        case CORE_DATA_IMMUTABLE:
        case CORE_DATA_MUTABLE:
        {
            CoreINT_U8 * bytes = (CoreINT_U8 *) __CoreData_getBytesPtr(me);
            memmove(
                buffer,
                bytes + range.offset,
                (size_t) range.length
            ); // memmove for sure... in case of someone calls this 
               // with my value as a buffer 
            break;
        case CORE_DATA_MUTABLE_STORAGE:
            break;                    
        }
    }
}


CORE_INLINE CoreINT_U32
__CoreData_getCapacity(CoreImmutableDataRef me)
{
    return me->_capacity; 
}

// Call only on mutable objects.
CORE_INLINE void
__CoreData_setCapacity(CoreDataRef me, CoreINT_U32 newCapacity)
{
    me->_capacity = newCapacity;
}


// Call only on mutable objects with non-inline buffer.
CORE_INLINE void
__CoreData_setContentPtr(CoreDataRef me, const void * contentPtr)
{
    me->_content = (void *) contentPtr;
}


static CoreHashCode 
__CoreData_hash(CoreObjectRef me)
{
    CoreINT_U32 result = 0;
    CoreImmutableDataRef _me = (CoreImmutableDataRef) me;
    CoreINT_U32 length = __CoreData_getLength(_me);
    
    //return Core_hashBytes(bytes, length);
        
    if (length > 0)
    {
        const CoreINT_U8 * bytes = __CoreData_getBytesPtr(_me);
        CoreINT_U32 idx, n;
        
        if (length < 64)
        {
            CoreINT_U32 end4 = (length & ~3);
            
            for (idx = 0; idx < end4; idx += 4)
            {
                result = result * 67503105 + bytes[idx + 0] * 16974593 + 
                         bytes[idx + 1] * 66049 + bytes[idx + 2] * 257 + 
                         bytes[idx + 3];
            }
            for ( ; idx < length; idx++)
            {
                result = result * 257 + bytes[idx];
            }
        }
        else
        {
            for (idx = 0, n = 16; idx < n; idx += 4)
            {
                result = result * 67503105 + bytes[idx + 0] * 16974593 + 
                         bytes[idx + 1] * 66049 + bytes[idx + 2] * 257 + 
                         bytes[idx + 3];
            }
            for (idx = (length >> 1) - 8, n = idx + 16; idx < n; idx += 4)
            {
                result = result * 67503105 + bytes[idx + 0] * 16974593 + 
                         bytes[idx + 1] * 66049 + bytes[idx + 2] * 257 + 
                         bytes[idx + 3];
            }
            for (idx = length - 16, n = idx + 16; idx < n; idx += 4)
            {
                result = result * 67503105 + bytes[idx + 0] * 16974593 + 
                         bytes[idx + 1] * 66049 + bytes[idx + 2] * 257 + 
                         bytes[idx + 3];
            }
        }
    }

    return result + (result << (length & 31));	
}

// length must always be > 0
static CoreComparison
__CoreData_compare(const void * buf1, const void * buf2, CoreINT_U32 length)
{
    CoreComparison result;
    
    int res = memcmp(buf1, buf2, length);
    result = (res < 0)
                ? CORE_COMPARISON_LESS_THAN
                : (res > 0)
                    ? CORE_COMPARISON_GREATER_THAN
                    : CORE_COMPARISON_EQUAL;
    
    return result;
}

static CoreBOOL
__CoreData_equal(CoreObjectRef me, CoreObjectRef to)
{
    CoreBOOL result = false;
    CORE_IS_DATA_RET1(me, false);
    CORE_IS_DATA_RET1(to, false);
    
    if (me == to)
    {
        result = true;
    }
    else
    {
        // Some optimization checks before calling a compare function...
        CoreImmutableDataRef _me = (CoreImmutableDataRef) me;
        CoreImmutableDataRef _to = (CoreImmutableDataRef) to;
        CoreINT_U32 meLength = CoreData_getLength(_me);
        CoreINT_U32 toLength = CoreData_getLength(_to);
        
        if (meLength == toLength)
        {
            if (meLength == 0)
            {
                result = true;
            }
            else
            {
                const void * meBuffer = __CoreData_getBytesPtr(me);
                const void * toBuffer = __CoreData_getBytesPtr(to);
                
                result = __CoreData_compare(meBuffer, toBuffer, meLength)
                    ? true : false;
            }
        }
    }
    
    return result;
}


CORE_INLINE CoreINT_U32
__CoreData_roundUpCapacity(CoreINT_U32 capacity)
{
    return (capacity <= CORE_DATA_MINIMAL_CAPACITY)
        ? CORE_DATA_MINIMAL_CAPACITY
        : (capacity < 1024)
            ? (1 << (CoreINT_U32)(CoreBits_mostSignificantBit(capacity - 1) + 1))
            : (CoreINT_U32)((capacity * 3) / 2);
}


static CoreImmutableStringRef 
__CoreData_getCopyOfDescription(CoreImmutableDataRef me)
{
    CoreStringRef result = null;
    
    CORE_IS_DATA_RET1(me, null);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);

    result = CoreString_create(null, 0);
    if (result != null)
    {
        char s[255];
        CoreINT_U32 length = __CoreData_getLength(me);
        
        sprintf(s, "CoreData <%p> : \n{\n\tlength = %u\n\tbytes:\n\t",
            me, length);
        CoreString_appendASCIICharacters(result, s, strlen(s));
        
        if (length > 32)
        {
            CoreINT_U32 idx;
            const CoreINT_U8 * ptr = __CoreData_getBytesPtr(me);
            
            for (idx = 0; idx < 16; idx += 4)
            {
                sprintf(s, "<%02x><%02x><%02x><%02x>", 
                    ptr[idx], ptr[idx+1], ptr[idx+2], ptr[idx+3]);
                CoreString_appendASCIICharacters(result, s, strlen(s));
            }
            sprintf(s, "\n\t ... \n\t");
            CoreString_appendASCIICharacters(result, s, strlen(s));
            for (idx = length - 16; idx < length; idx += 4)
            {
                sprintf(s, "<%02x><%02x><%02x><%02x>", 
                    ptr[idx], ptr[idx+1], ptr[idx+2], ptr[idx+3]);
                CoreString_appendASCIICharacters(result, s, strlen(s));
            }
        }
        else
        {
            CoreINT_U32 idx;
            const CoreINT_U8 * ptr = __CoreData_getBytesPtr(me);
            
            for (idx = 0; idx < length; idx++)
            {
                if (idx == 15)
                {
                    sprintf(s, "\n\t");
                    CoreString_appendASCIICharacters(result, s, strlen(s));
                }
                sprintf(s, "<%02x>", ptr[idx]);
                CoreString_appendASCIICharacters(result, s, strlen(s));
            }
        }
        
        sprintf(s, "\n}");
        CoreString_appendASCIICharacters(result, s, strlen(s) + 1); // +1 for EOS
    }
        
    return (CoreImmutableStringRef) result;
}


static void
__CoreData_cleanup(CoreObjectRef me)
{
    CoreImmutableDataRef _me = (CoreImmutableDataRef) me;
    CoreAllocatorRef allocator = _me->_bytesDeallocator;
    void * memory = __CoreData_getBytesPtr(me);
    
    if (allocator != null)
    {
        CoreAllocator_deallocate(allocator, memory);
        Core_release(allocator);
    }
}


static void
__CoreData_switchToStorage(CoreDataRef me)
{
    /*
     * Empty until STORAGE introduce.
     */     
}


/*
 * Capable to expand only non-fixed (external) mutable variants. 
 * This must be ensured by a caller. 
 */   
static CoreBOOL
__CoreData_ensureAddCapacity(CoreDataRef me, CoreINT_U32 needed)
{
    CoreBOOL result = false;
    CoreINT_U32 neededCapacity = __CoreData_getLength(me) + needed;
    void * content = __CoreData_getBytesPtr(me);
    
    if (neededCapacity < CORE_DATA_MAXIMAL_CAPACITY)
    {
        CoreINT_U32 capacity = __CoreData_roundUpCapacity(neededCapacity);
        CoreAllocatorRef allocator;
        
        allocator = (me->_bytesDeallocator == null) 
            ? Core_getAllocator(me)
            : me->_bytesDeallocator;
        if (content == null)
        {
            content = CoreAllocator_allocate(
                allocator,
                capacity * sizeof(CoreINT_U8)
            );
            __CoreData_setContentPtr(me, content);
        }
        else if (neededCapacity > CORE_DATA_STORAGE_SWITCH)
        {
            __CoreData_switchToStorage(me);
        }
        else
        {
            __CoreData_setContentPtr(
                me, 
                CoreAllocator_reallocate(
                    allocator,
                    content,
                    capacity * sizeof(CoreINT_U8)
                )
            );        
        }
        result = (__CoreData_getBytesPtr(me) != null) ? true : false;
        if (result)
        {
            __CoreData_setCapacity(me, capacity);
        }
    }
    
    return result;
}


CORE_INLINE CoreBOOL
__CoreData_accomodateLength(CoreDataRef me, CoreINT_U32 newLength)
{
    CoreBOOL result = true;
    CoreINT_U32 length = __CoreData_getLength(me);
    CoreINT_U32 oldCapacity = __CoreData_getCapacity(me);
    
    if (__CoreData_isMutable(me))
    {
        if (__CoreData_isFixed(me))
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
                result = __CoreData_ensureAddCapacity(me, newLength - length);
            }
        }
    }
    
    if (result)
    {
        //
        // Question: Should we reset the bytes?
        //
        __CoreData_setLength(me, newLength);    
    }
    
    return result;
}

static CoreBOOL
__CoreData_ensureRemoveCapacity(CoreDataRef me, CoreINT_U32 changedLength)
{
    return true;
}

/* does no range check... must be ensured by caller */
static CoreBOOL
__CoreData_replaceBytesInRange(
    CoreDataRef me, 
    CoreRange range,
    const void * bytes, 
    CoreINT_U32 length
)
{
    CoreBOOL result = false;
    CoreINT_S32 change = (CoreINT_S32) length - range.length;
    CoreINT_U32 curLength = __CoreData_getLength(me);
    CoreINT_U32 newLength = (CoreINT_U32) ((CoreINT_S32) curLength + change);
    CoreINT_U32 capacity = __CoreData_getCapacity(me);
    CoreINT_U8 * content = __CoreData_getBytesPtr(me);

    //
    // Currently only for inlined and external mutable buffer.
    // Note that newLength can never be < 0.
    //
    if (__CoreData_isMutable(me))
    {
        if (__CoreData_isInline(me))
        {
            if (capacity >= newLength)
            {
                result = true;
            }
        }
        else
        {
            result = true;
            if (curLength < newLength)
            {
                if ((content == NULL) || (capacity < newLength))
                {
                    result = __CoreData_ensureAddCapacity(me, (CoreINT_U32) change);
                }
            }
            else
            {
                result = __CoreData_ensureRemoveCapacity(me, (CoreINT_U32) change);
            }
        }
    }
    
    // Currently only for non-storage mutable variant.
    if (result && (__CoreData_getType(me) != CORE_DATA_MUTABLE_STORAGE))
    {
        //
        // If needed, rearrange bytes in my own value.
        //
        if ((change != 0) && (range.offset + range.length < curLength))
        { 
            memmove(
                (void *) (content + range.offset + length),
                (const void *) (content + range.offset + range.length),
                (size_t) ((curLength - range.offset - range.length) 
                            * sizeof(CoreINT_U8))
            );
        }
        
        //
        // When the length is positive, copy bytes.
        //
        if (length > 0) 
        {
            memmove(
                (void *) (content + range.offset),
                (const void *) bytes,
                (size_t) (length * sizeof(CoreINT_U8))
            ); // memmove for sure... in case of someone calls this 
               // with my value as bytes
        }

        // Update the length.
        __CoreData_setLength(me, newLength);
    }
	
	return result;
}



static const CoreClass __CoreDataClass =
{
    0x00,                           // version
    "CoreData",                     // name
    NULL,                           // init
    NULL,                           // copy
    __CoreData_cleanup,             // cleanup
    __CoreData_equal,               // equal
    __CoreData_hash,                // hash
    __CoreData_getCopyOfDescription // getCopyOfDescription
};

/* CORE_PROTECTED */ void
CoreData_initialize(void)
{
    CoreDataID = CoreRuntime_registerClass(&__CoreDataClass);
}


static CoreDataRef
__CoreData_init(
    CoreAllocatorRef allocator,
	CoreBOOL isMutable,
	const void * bytes,
	CoreINT_U32 length, // length of bytes
	CoreINT_U32 maxCapacity,
	CoreAllocatorRef bytesAllocator
)
{
    struct __CoreData * result = null;
    CoreINT_U32 size;
    CoreBOOL isFixed, isInline, noCopy;

    size = sizeof(struct __CoreData);
    isFixed = (!isMutable || (maxCapacity != 0)) ? true : false;
    noCopy = (bytesAllocator != null) ? true : false;
    isInline = (isFixed && !noCopy) ? true : false;
    
    if (isInline)
    {
        size += maxCapacity * sizeof(CoreINT_U8);
    }
        
    result = (struct __CoreData *) CoreRuntime_createObject(
        allocator, 
        CoreDataID, 
        size
    );
    if (result != null)
    {
        __CoreData_setMutable(result, isMutable);
        __CoreData_setFixed(result, isFixed);
        __CoreData_setInline(result, isInline);
        __CoreData_setLength(result, length);
        __CoreData_setCapacity(result, maxCapacity);
        __CoreData_setContentPtr(result, NULL);
        result->_bytesDeallocator = null;
        
        if (!isMutable)
        {
            __CoreData_setType(result, CORE_DATA_IMMUTABLE);
        }
        else
        {
            __CoreData_setType(result, CORE_DATA_MUTABLE);
        }
        if (noCopy)
        {
            __CoreData_setContentPtr(result, bytes);
            result->_bytesDeallocator = Core_retain(bytesAllocator);
        }
        else if (isFixed)
        {
            void * content = (CoreINT_U8 *) result + sizeof(struct __CoreData);
            __CoreData_setContentPtr(result, content);
            if (!isMutable)
            {
                memcpy(
                    content, 
                    bytes, 
                    __CoreData_getLength(result) * sizeof(CoreINT_U8)
                );
            }
        }
        else 
        {
            // Nothing...
        }
    }
    
    return (CoreDataRef) result;    
}


/* CORE_PUBLIC */ CoreDataRef
CoreData_create(
    CoreAllocatorRef allocator,
    CoreINT_U32 maxCapacity
)
{
    return __CoreData_init(
        allocator,
        true,
        null,
        0,
        maxCapacity,
        null
    );        
}


/* CORE_PUBLIC */ CoreImmutableDataRef
CoreData_createImmutable(
    CoreAllocatorRef allocator,
    const void * bytes,
    CoreINT_U32 length
)
{
    return __CoreData_init(
        allocator,
        false,
        bytes,
        length,
        length,
        null
    );        
}


/* CORE_PUBLIC */ CoreDataRef
CoreData_createWithExternalBytesNoCopy(
    CoreAllocatorRef allocator,
    const void * bytes,
    CoreINT_U32 length,
    CoreINT_U32 capacity,
    CoreAllocatorRef bytesAllocator
)
{
    CORE_ASSERT_RET1(
        null,
        (bytes != null) || (length == 0),
        CORE_LOG_ASSERT,
        "%s(): bytes cannot be NULL when length is > 0",
        __PRETTY_FUNCTION__
    );

    return __CoreData_init(
        allocator,
        true,
        bytes,
        length,
        capacity,
        (bytesAllocator == null)
            ? CoreAllocator_getDefault() : bytesAllocator
    );
}


/* CORE_PUBLIC */ CoreImmutableDataRef
CoreData_createImmutableWithBytesNoCopy(
    CoreAllocatorRef allocator,
    const void * bytes,
    CoreINT_U32 length,
    CoreAllocatorRef bytesDeallocator
)
{
    CORE_ASSERT_RET1(
        null,
        (bytes != null) || (length == 0),
        CORE_LOG_ASSERT,
        "%s(): bytes cannot be NULL when length is > 0",
        __PRETTY_FUNCTION__
    );
            
    return __CoreData_init(
        allocator,
        false,
        bytes,
        length,
        length,
        (bytesDeallocator == null) 
            ? CoreAllocator_getDefault() : bytesDeallocator
    );        
}


/* CORE_PUBLIC */ CoreDataRef
CoreData_createCopy(
    CoreAllocatorRef allocator,
    CoreINT_U32 maxCapacity,
    CoreImmutableDataRef data
)
{
    CoreINT_U32 length = CoreData_getLength(data);
    
    CORE_ASSERT_RET1(
        null,
        (maxCapacity == 0) || (maxCapacity >= length), 
        CORE_LOG_ASSERT,
        "%s(): maxCapacity cannot be less than data's length",
        __PRETTY_FUNCTION__
    );
            
    return __CoreData_init(
        allocator,
        true,
        CoreData_getConstBytesPtr(data),
        length,
        maxCapacity,
        null        
    );
}


/* CORE_PUBLIC */ CoreImmutableDataRef
CoreData_createImmutableCopy(
    CoreAllocatorRef allocator,
    CoreImmutableDataRef data
)
{
    CoreImmutableDataRef result = null;
    
    CORE_ASSERT_RET1(
        null,
        data != NULL,
        CORE_LOG_ASSERT,
        "%s(): cannot create copy of null object", __PRETTY_FUNCTION__
    );
        
    if (!__CoreData_isMutable(data))
    {
        result = (CoreImmutableDataRef) Core_retain(data);
    }
    else
    {
        CoreINT_U32 length = CoreData_getLength(data);
        result = __CoreData_init(
            allocator,
            false,
            CoreData_getBytesPtr(data),
            length,
            length,
            null        
        );
    }
    
    return result;
}    


/* CORE_PUBLIC */ CoreINT_U32
CoreData_getLength(CoreImmutableDataRef me)
{
    CORE_IS_DATA_RET1(me, 0);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    
    return __CoreData_getLength(me);
}


/* CORE_PUBLIC */ const CoreINT_U8 *
CoreData_getConstBytesPtr(CoreImmutableDataRef me)
{
    CORE_IS_DATA_RET1(me, null);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    
    return (const CoreINT_U8 *) __CoreData_getBytesPtr(me);
}


/* CORE_PUBLIC */ CoreINT_U8 *
CoreData_getBytesPtr(CoreImmutableDataRef me)
{
    CORE_IS_DATA_RET1(me, null);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        null,
        (__CoreData_getType(me) != CORE_DATA_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    
    return (CoreINT_U8 *) __CoreData_getBytesPtr(me);
}


/* CORE_PUBLIC */ const CoreINT_U8 *
CoreData_iterateConstBytes(CoreImmutableDataRef me, CoreINT_U32 * pIter)
{
    CORE_IS_DATA_RET1(me, null);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        null,
        pIter != null,
        CORE_LOG_ASSERT,
        "%s(): pIter cannot be null-pointer", __PRETTY_FUNCTION__
    );
    
    return __CoreData_iterateConstBytes(me, pIter);
}


/* CORE_PUBLIC */ CoreINT_U8 *
CoreData_iterateBytes(CoreImmutableDataRef me, CoreINT_U32 * pIter)
{
    CORE_IS_DATA_RET1(me, null);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        null,
        (__CoreData_getType(me) != CORE_DATA_IMMUTABLE),
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
    
    return (CoreINT_U8 *) __CoreData_iterateConstBytes(me, pIter);
}


/* CORE_PUBLIC */ void
CoreData_copyBytes(CoreImmutableDataRef me, void * buffer)
{
    CORE_IS_DATA_RET0(me);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET0(
        (buffer != null),
        CORE_LOG_ASSERT,
        "%s(): buffer cannot be null-pointer", __PRETTY_FUNCTION__
    );

    __CoreData_copyBytesInRange(
        me, 
        CoreRange_create(0, __CoreData_getLength(me)),
        buffer
    );
}


/* CORE_PUBLIC */ CoreBOOL
CoreData_copyBytesInRange(CoreImmutableDataRef me, CoreRange range, void * buffer)
{
    CORE_IS_DATA_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        range.offset + range.length <= __CoreData_getLength(me),
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
    
    __CoreData_copyBytesInRange(me, range, buffer);
    
    return true;
}


/* CORE_PUBLIC */ CoreComparison
CoreData_compare(CoreImmutableDataRef me, CoreImmutableDataRef to)
{
    CoreComparison result = CORE_COMPARISON_UNCOMPARABLE;

    CORE_IS_DATA_RET1(me, CORE_COMPARISON_UNCOMPARABLE);
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
        const void * meBuffer = __CoreData_getBytesPtr(me);
        const void * toBuffer = __CoreData_getBytesPtr(to);
        
        if ((meBuffer != null) && (toBuffer != null))
        {
            CoreINT_U32 meLength = __CoreData_getLength(me);
            CoreINT_U32 toLength = __CoreData_getLength(to);
            CoreINT_U32 cmpLength = min(meLength, toLength);
            if (cmpLength > 0)
            {
                result = __CoreData_compare(meBuffer, toBuffer, cmpLength);
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
CoreData_setLength(CoreDataRef me, CoreINT_U32 newLength)
{
    CORE_IS_DATA_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreData_getType(me) != CORE_DATA_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );

    return __CoreData_accomodateLength(me, newLength);        
}


/* CORE_PUBLIC */ void
CoreData_setExternalBytesNoCopy(
    CoreDataRef me,
    const void * bytes,
    CoreINT_U32 length,
    CoreINT_U32 capacity 
)
{
    CORE_IS_DATA_RET0(me);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET0(
        (__CoreData_getType(me) == CORE_DATA_MUTABLE) && 
        (me->_bytesDeallocator != null),
        CORE_LOG_ASSERT,
        "%s(): the data object must have been created by "
        "createWithExternalBytesNoCopy constructor!",
        __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET0(
        (length <= capacity) && ((bytes != null) || (capacity == 0)),
        CORE_LOG_ASSERT,
        "%s(): invalid args: bytes %p, length %u, capacity %u",
        __PRETTY_FUNCTION__, bytes, length, capacity
    );

    __CoreData_setContentPtr(me, bytes);
    __CoreData_setLength(me, length);
    __CoreData_setCapacity(me, capacity);    
}


/* CORE_PUBLIC */ void
CoreData_clear(CoreDataRef me)
{
    (void) CoreData_setLength(me, 0);
}


/* CORE_PUBLIC */ CoreBOOL
CoreData_removeBytesInRange(CoreDataRef me, CoreRange range)
{
    CORE_IS_DATA_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreData_getType(me) != CORE_DATA_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET1(
        false,
        __CoreData_getLength(me) > 0,
        CORE_LOG_ASSERT,
        "%s(): there is no content to be removed.", __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET1(
        false,
        range.offset + range.length <= __CoreData_getLength(me),
        CORE_LOG_ASSERT,
        "%s(): parameter range out of bounds.", __PRETTY_FUNCTION__
    );
        
    return __CoreData_replaceBytesInRange(me, range, null, 0);    
}


/* CORE_PUBLIC */ CoreBOOL
CoreData_appendBytes(CoreDataRef me, const void * bytes, CoreINT_U32 length)
{
    CORE_IS_DATA_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreData_getType(me) != CORE_DATA_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET1(
        false,
        (bytes != null) || (length == 0),
        CORE_LOG_ASSERT,
        "%s(): bytes cannot be null-pointer if length is %u > 0", 
        __PRETTY_FUNCTION__, length
    );
    
    return __CoreData_replaceBytesInRange(
        me, 
        CoreRange_create(__CoreData_getLength(me), 0),
        bytes, 
        length
    );    
}


/* CORE_PUBLIC */ CoreBOOL
CoreData_insertBytes(
    CoreDataRef me, 
    CoreINT_U32 index,
    const void * bytes, 
    CoreINT_U32 length
)
{
    CORE_IS_DATA_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreData_getType(me) != CORE_DATA_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET1(
        false,
        (index <= __CoreData_getLength(me)),
        CORE_LOG_ASSERT,
        "%s(): index %u is out of bounds", 
        __PRETTY_FUNCTION__, length
    );
    CORE_ASSERT_RET1(
        false,
        (bytes != null) || (length == 0),
        CORE_LOG_ASSERT,
        "%s(): bytes cannot be null-pointer if length is %u > 0", 
        __PRETTY_FUNCTION__, length
    );
    
    return __CoreData_replaceBytesInRange(
        me, 
        CoreRange_create(index, 0),
        bytes, 
        length
    );    
}


/* CORE_PUBLIC */ CoreBOOL
CoreData_replaceBytesInRange(
    CoreDataRef me, 
    CoreRange range,
	const void * bytes, 
    CoreINT_U32 length
)
{
    CORE_IS_DATA_RET1(me, false);
    CORE_DUMP_OBJ_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        (__CoreData_getType(me) != CORE_DATA_IMMUTABLE),
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET1(
        false,
        range.offset + range.length <= __CoreData_getLength(me),
        CORE_LOG_ASSERT,
        "%s(): parameter range out of bounds", __PRETTY_FUNCTION__
    );

    return __CoreData_replaceBytesInRange(me, range, bytes, length);    
}


