

/*****************************************************************************
 *
 * Includes
 * 
 *****************************************************************************/

#include <CoreFramework/CoreData.h>
#include "CoreInternal.h"
#include "CoreRuntime.h"
#include <stdlib.h>



/*****************************************************************************
 *
 * Types definitions
 * 
 ****************************************************************************/

/*
typedef struct __CoreDataBuffer
{
    CoreINT_U32 capacity;
    CoreAllocatorRef bytesAllocator;
    void * storage; // either inline or pointer to external storage
} __CoreDataBuffer;

typedef struct __CoreDataImmutable
{
    CoreRuntimeObject core;
    CoreINT_U32 length;
    CoreAllocatorRef bytesDeallocator;
    void * storage; // either inline or pointer to external storage   
} __CoreDataImmutable;

typedef struct __CoreDataFixedMutable
{
    CoreRuntimeObject core;
    CoreINT_U32 length;
    CoreINT_U32 capacity;
    // inline bytes     
} __CoreDataFixedMutable;

typedef struct __CoreDataMutable
{
    CoreRuntimeObject core;
    CoreINT_U32 length;
    void * storage; // either __CoreDataBuffer or storage (not available yet)
} __CoreDataMutable;

// common opaque type
struct __CoreData
{
    CoreRuntimeObject core;
    CoreINT_U32 length;
    union 
    {
        CoreINT_U32 capacity;
        CoreAllocatorRef bytesDeallocator;
        void * storage;
    };
};


struct __CoreData
{
    CoreRuntimeObject core;
    CoreINT_U32 length;
    // either inline bytes (Immutable) or following variants: 
    // Note: in case of external storages, pointer to the buffer is always 
    //   first. Or at least must be on the same position in the structure. 
    union
    {
        void * storage; 
        struct __DataImmutableExternal
        {
            void * storage;
            CoreAllocatorRef bytesDeallocator; // only deallocate
        } immutableExternal;
        struct __DataMutableInline // for fixed mutable
        {
            CoreINT_U32 capacity;
            // inline bytes         
        } mutableInline;
        struct __DataMutableExternal
        {
            void * storage;
            CoreINT_U32 capacity;
            CoreAllocatorRef bytesAllocator;
        } mutableExternal;
    } value;
}
*/

struct __CoreData
{
    CoreRuntimeObject core;
    CoreINT_U32 length;
};

typedef struct __DataImmutableExternal
{
    CoreRuntimeObject core;
    CoreINT_U32 length;
    void * storage;
    CoreAllocatorRef bytesDeallocator;
} __DataImmutableExternal;

typedef struct __DataMutableInline
{
    CoreRuntimeObject core;
    CoreINT_U32 length;
    CoreINT_U32 capacity;
} __DataMutableInline;

typedef struct __DataMutableExternal
{
    CoreRuntimeObject core;
    CoreINT_U32 length;
    void * storage;
    CoreAllocatorRef bytesAllocator;
    CoreINT_U32 capacity;
} __DataMutableExternal;


typedef enum CoreDataType
{
    CORE_DATA_IMMUTABLE         = 1,
    CORE_DATA_MUTABLE_BUFFER    = 2,
    CORE_DATA_MUTABLE_STORAGE   = 3
} CoreDataType; 

/*
typedef enum CoreDataType
{
    CORE_DATA_IMMUTABLE         = 1,
    CORE_DATA_FIXED_MUTABLE     = 2,
    CORE_DATA_MUTABLE_BUFFER    = 3,
    CORE_DATA_MUTABLE_STORAGE   = 4
} CoreDataType; 
*/


static CoreClassID CoreDataID = CORE_CLASS_ID_UNKNOWN;



#define CORE_DATA_MINIMAL_CAPACITY      16UL
#define CORE_DATA_MAXIMAL_CAPACITY      (1 << 31)
#define CORE_DATA_MUTABLE_BUFFER_LIMIT  CORE_DATA_MAXIMAL_CAPACITY





#define CORE_IS_DATA(data) CORE_VALIDATE_OBJECT(data, CoreDataID)
#define CORE_IS_DATA_RET0(data) \
    do { if(!CORE_IS_DATA(data)) return ;} while (0)
#define CORE_IS_DATA_RET1(data, ret) \
    do { if(!CORE_IS_DATA(data)) return (ret);} while (0)



//
// Custom object's information:
//  - type of data in 0-1 bits
//  - has external storage info on 2nd bit
//  - has fixed length info on 3rd bit
//

#define CORE_DATA_TYPE_START            0
#define CORE_DATA_TYPE_LENGTH           2

#define CORE_DATA_EXTERNAL_START        2
#define CORE_DATA_EXTERNAL_LENGTH       1

#define CORE_DATA_IS_FIXED_START        3
#define CORE_DATA_IS_FIXED_LENGTH       1


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
__CoreData_hasExternalStorage(CoreImmutableDataRef me)
{
    return (CoreBOOL) CoreBitfield_getValue(
        ((const CoreRuntimeObject *) me)->info,
        CORE_DATA_EXTERNAL_START,
        CORE_DATA_EXTERNAL_LENGTH
    );
}

CORE_INLINE void
__CoreData_setHasExternalStorage(CoreImmutableDataRef me, CoreBOOL value)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) me)->info,
        CORE_DATA_EXTERNAL_START,
        CORE_DATA_EXTERNAL_LENGTH,
        (CoreINT_U32) value
    );
}

CORE_INLINE CoreBOOL
__CoreData_isFixed(CoreImmutableDataRef me)
{
    return (CoreBOOL) CoreBitfield_getValue(
        ((const CoreRuntimeObject *) me)->info,
        CORE_DATA_IS_FIXED_START,
        CORE_DATA_IS_FIXED_LENGTH
    );
}

CORE_INLINE void
__CoreData_setFixed(CoreImmutableDataRef me, CoreBOOL value)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) me)->info,
        CORE_DATA_IS_FIXED_START,
        CORE_DATA_IS_FIXED_LENGTH,
        (CoreINT_U32) value
    );
}


CORE_INLINE const CoreINT_U8 *
__CoreData_getBytesPtr(CoreImmutableDataRef me)
{
    CoreINT_U8 * result = null;
    
    if (__CoreData_hasExternalStorage(me))
    {
        result = (CoreINT_U8 *) ((__DataImmutableExternal *) me)->storage; 
    }
    else
    {
        // inlined buffer
        switch (__CoreData_getType(me))
        {
            case CORE_DATA_IMMUTABLE:
                result = ((CoreINT_U8 *) me) + sizeof(struct __CoreData);
                break;
            case CORE_DATA_MUTABLE_BUFFER:
                result = ((CoreINT_U8 *) me) + sizeof(__DataMutableExternal);
                break;
        }
    }
    
    return (const CoreINT_U8 *) result;
}


CORE_INLINE CoreINT_U8
__CoreData_getByteAtIndex(CoreImmutableDataRef me, CoreIndex index)
{
    CoreINT_U8 result = 0;
    
    switch (__CoreData_getType(me))
    {
        case CORE_DATA_IMMUTABLE:
        case CORE_DATA_MUTABLE_BUFFER:
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
	return me->length;
}	


CORE_INLINE void 
__CoreData_setLength(CoreDataRef me, CoreINT_U32 newLength)
{
    me->length = newLength;
}


CORE_INLINE const void *
__CoreData_iterateConstBytes(CoreImmutableDataRef me, CoreINT_U32 * pIter)
{
    const void * result = null;
    CoreINT_U32 length = __CoreData_getLength(me);
        
    if (*pIter <= length)
    {
        switch (__CoreData_getType(me))
        {
            case CORE_DATA_IMMUTABLE:
            case CORE_DATA_MUTABLE_BUFFER:
                result = __CoreData_getBytesPtr(me);
                *pIter = length - *pIter;
                break;
            
            case CORE_DATA_MUTABLE_STORAGE:
                break;                    
        }
    }
    
    return result;
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
        case CORE_DATA_MUTABLE_BUFFER:
        {
            const CoreINT_U8 * bytes = __CoreData_getBytesPtr(me);
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
    CoreINT_U32 result = 0;
    CoreDataType type = __CoreData_getType(me);
    
    if (type == CORE_DATA_IMMUTABLE)
    {
        result = me->length;
    }
    else
    {
        switch (type)
        {
            case CORE_DATA_MUTABLE_BUFFER:
                result = (__CoreData_hasExternalStorage(me))
                    ? ((__DataMutableExternal *) me)->capacity
                    : ((__DataMutableInline *) me)->capacity;
                break;
            case CORE_DATA_MUTABLE_STORAGE:
                /* currently not available */
                break;
        }
    }

    return result; 
}


static CoreINT_U32 
__CoreData_hash(CoreObjectRef me)
{
    CoreINT_U32 result = 0;
    CoreImmutableDataRef _me = (CoreImmutableDataRef) me;
    CoreINT_U32 length = __CoreData_getLength(_me);
    const CoreINT_U8 * bytes = __CoreData_getBytesPtr(_me);
    
    //return Core_hashBytes(bytes, length);
        
    if (length > 0)
    {
        const CoreINT_U8 * bytes = __CoreData_getBytesPtr(_me);
        CoreINT_U32 idx, n;
        
        if (length < 64)
        {
            CoreINT_U32 end4 = length & ~3;
            
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
        CoreImmutableDataRef _me = (CoreImmutableDataRef) me;
        CoreImmutableDataRef _to = (CoreImmutableDataRef) to;
        CoreINT_U32 meLength = CoreData_getLength(_me);
        CoreINT_U32 toLength = CoreData_getLength(_to);
        
        if (meLength == toLength)
        {
            result = (CoreData_compare(_me, _to) == CORE_COMPARISON_EQUAL)
                ? true : false;
        }
    }
    
    return result;
}


CORE_INLINE CoreINT_U32
__CoreDataBuffer_roundUpCapacity(CoreINT_U32 capacity)
{
    return (capacity <= CORE_DATA_MINIMAL_CAPACITY)
        ? CORE_DATA_MINIMAL_CAPACITY
        : (capacity < 1024)
            ? (1 << (CoreINT_U32)(CoreBits_mostSignificantBit(capacity - 1) + 1))
            : (CoreINT_U32)((capacity * 3) / 2);
}


static char *
__CoreData_getCopyOfDescription(CoreObjectRef me)
{
    return null;
}


static void
__CoreData_cleanup(CoreObjectRef me)
{
    CoreImmutableDataRef _me = (CoreImmutableDataRef) me;
    CoreBOOL isExternal = __CoreData_hasExternalStorage(_me);
    
    if (isExternal)
    {
        CoreDataType type = __CoreData_getType(_me);        
        CoreAllocatorRef allocator;
        void * memory;
        
        switch (type)
        {
            case CORE_DATA_IMMUTABLE:
                allocator = ((__DataImmutableExternal *) _me)->bytesDeallocator;
                memory = ((__DataImmutableExternal *) _me)->storage;
                break;
            case CORE_DATA_MUTABLE_BUFFER:
                if (((__DataMutableExternal *) _me)->bytesAllocator != null)
                {
                    allocator = ((__DataMutableExternal *) _me)->bytesAllocator;
                }
                else
                {
                    allocator = Core_getAllocator(me); 
                }
                memory = ((__DataMutableExternal *) _me)->storage;
                break;
        }
        if (allocator != null)
        {
            CoreAllocator_deallocate(
                allocator, 
                memory
            );
            Core_release(allocator);
        }    
    }
}


/*
 * Capable to expand only non-fixed mutable variants. This must be ensured
 * by a caller. 
 * Currently STORAGE variant is not available.
 */   
static CoreBOOL
__CoreData_expand(CoreDataRef me, CoreINT_U32 needed)
{
    CoreBOOL result = false;
    CoreINT_U32 neededCapacity = me->length + needed;
    
    if (neededCapacity < CORE_DATA_MUTABLE_BUFFER_LIMIT)
    {
        __DataMutableExternal * _me = (__DataMutableExternal *) me;
        CoreINT_U32 capacity = __CoreDataBuffer_roundUpCapacity(neededCapacity);
        CoreAllocatorRef allocator;
        
        allocator = (_me->bytesAllocator == null) 
            ? Core_getAllocator(me)
            : _me->bytesAllocator;
        if (_me->storage == null)
        {
            _me->storage = CoreAllocator_allocate(
                allocator,
                capacity * sizeof(CoreINT_U8)
            );
            result = (_me->storage != null) ? true : false;                 
        }
        else
        {
            _me->storage = CoreAllocator_reallocate(
                allocator,
                _me->storage,
                capacity * sizeof(CoreINT_U8)
            );
            result = (_me->storage != null) ? true : false;              
        }
        if (result)
        {
            _me->capacity = capacity;
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
    
    switch (__CoreData_getType(me))
    {
        case CORE_DATA_MUTABLE_BUFFER:
        case CORE_DATA_MUTABLE_STORAGE:
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
                    result = __CoreData_expand(me, newLength - length);
                }
            }
            break;
        }
    }
    
    if (result)
    {
        /*
         * Question: should we reset bytes only when newLength is greater
         * than data's capacity?
         */                  
        if (length < newLength)
        //if (newLength > oldCapacity)
        {
            memset(
                (void *) (__CoreData_getBytesPtr(me) + length),
                0,
                newLength - length
            );
        }
        __CoreData_setLength(me, newLength);    
    }
    
    return result;
}


/* does no range check... must be ensured by caller */
static CoreBOOL
_CoreData_replaceBytesInRange(
    CoreDataRef me, 
    CoreRange range,
    const void * bytes, 
    CoreINT_U32 length
)
{
    CoreBOOL result = false;
    CoreINT_S32 change = (CoreINT_S32) length - range.length;
    CoreINT_U32 newLength = (CoreINT_U32) ((CoreINT_S32) me->length + change);
    const CoreINT_U8 * value;

    //
    // Currently only for fixed mutable and mutable buffer.
    // Note that newLength can never be < 0.
    //
    switch (__CoreData_getType(me))
    {
        case CORE_DATA_MUTABLE_BUFFER:
        {
            if (__CoreData_hasExternalStorage(me))
            {
                __DataMutableExternal * _me = (__DataMutableExternal *) me;
                
                result = true;
                if ((_me->storage == NULL) || (_me->capacity < newLength))
                {
                    result = __CoreData_expand(me, (CoreINT_U32) change);
                }
            }
            else
            {
                __DataMutableInline * _me = (__DataMutableInline *) me;
                
                if (_me->capacity >= newLength)
                {
                    result = true;
                }
            }
            break;
        }
    }
    
    if (result)
    {
        value = __CoreData_getBytesPtr(me);
        
        //
        // If needed, rearrange bytes in my own value.
        //
        if ((change != 0) && (range.offset + range.length < me->length))
        { 
            memmove(
                (void *) (value + range.offset + length),
                (const void *) (value + range.offset + range.length),
                (size_t) ((me->length - range.offset - range.length) 
                            * sizeof(CoreINT_U8))
            );
        }
        
        //
        // When the length is positive, copy bytes.
        //
        if (change > 0) 
        {
            memmove(
                (void *) (value + range.offset),
                (const void *) bytes,
                (size_t) (length * sizeof(CoreINT_U8))
            ); // memmove for sure... in case of someone calls this 
               // with my value as bytes
        }

        // Update the length.
        me->length = newLength;
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
	CoreDataType type,
	const void * bytes,
	CoreINT_U32 length, // length of bytes
	CoreINT_U32 maxCapacity,
	CoreAllocatorRef bytesAllocator
)
{
    struct __CoreData * result = null;
    CoreINT_U32 size = 0;
    CoreBOOL isFixed = false;

    switch (type)
    {
        case CORE_DATA_IMMUTABLE:
        {
            // In case of null bytesDeallocator, we will copy all bytes.
            // Otherwise our value will just refer to an external storage.
            if (bytesAllocator == null)
            {
                size += sizeof(struct __CoreData) + 
                        maxCapacity * sizeof(CoreINT_U8);
            }
            else
            {
                size += sizeof(__DataImmutableExternal);
            }
            break;
        }
        case CORE_DATA_MUTABLE_BUFFER:
        {
            if (maxCapacity != 0)
            {
                isFixed = true;
                size += sizeof(__DataMutableInline) + 
                        maxCapacity * sizeof(CoreINT_U8);
            }
            else
            {
                size += sizeof(__DataMutableExternal);
            }
            break;
        }        
    }
        
    result = (struct __CoreData *) CoreRuntime_CreateObject(
        allocator, 
        CoreDataID, 
        size
    );
    if (result != null)
    {
        __CoreData_setType(result, type);
        __CoreData_setFixed(result, isFixed);
        
        switch (type)
        {
            case CORE_DATA_IMMUTABLE:
            {
                result->length = maxCapacity;

                // Copy the bytes only if bytesDeallocator is null. 
                // Otherwise just assign the bytes to my storage.
                if (bytesAllocator == null)
                {
                    memcpy(
                        (CoreINT_U8 *) result + sizeof(struct __CoreData), 
                        bytes, 
                        result->length * sizeof(CoreINT_U8)
                    );
                }
                else
                {
                    __DataImmutableExternal * me;
                    
                    me = (__DataImmutableExternal *) result; 
                    me->storage = (void *) bytes;
                    me->bytesDeallocator = Core_retain(bytesAllocator);
                    __CoreData_setHasExternalStorage(result, true);
                }
                break;
            }
			
            case CORE_DATA_MUTABLE_BUFFER:
            {
                if (isFixed)
                {
                    __DataMutableInline * me = (__DataMutableInline *) result;
                    me->length = 0;
                    me->capacity = maxCapacity;                    
                }
                else
                {
                    __DataMutableExternal * me = (__DataMutableExternal *) result;
                    me->length = 0;
                    me->capacity = maxCapacity;
                    me->storage = null;
                    __CoreData_setHasExternalStorage(result, true);
                    me->bytesAllocator = bytesAllocator;
                    if (bytesAllocator != null)
                    {
                        Core_retain(me->bytesAllocator);                    
                    }
                }
                break;
            }
        } 
    }
    else
    {
        if ((type == CORE_DATA_MUTABLE_BUFFER) && (isFixed))
        {
            //
            // Allocation was not successful (most probably the requested
            // size is too large). So try another attempt with STORAGE.
            //
            result = __CoreData_init(
                allocator,
                CORE_DATA_MUTABLE_STORAGE,
                bytes,
                length,
                maxCapacity,
                null
            );
        }
    }
    
    return (CoreDataRef) result;    
}


/* CORE_PUBLIC */ CoreDataRef
CoreData_Create(
    CoreAllocatorRef allocator,
    CoreINT_U32 maxCapacity
)
{
    return __CoreData_init(
        allocator,
        CORE_DATA_MUTABLE_BUFFER,
        null,
        0,
        maxCapacity,
        null
    );        
}


/* CORE_PUBLIC */ CoreImmutableDataRef
CoreData_CreateImmutable(
    CoreAllocatorRef allocator,
    const void * bytes,
    CoreINT_U32 length
)
{
    return __CoreData_init(
        allocator,
        CORE_DATA_IMMUTABLE,
        bytes,
        length,
        length,
        null
    );        
}


/* CORE_PUBLIC */ CoreDataRef
CoreData_CreateWithExternalBufferNoCopy(
    CoreAllocatorRef allocator,
    const void * buffer,
    CoreINT_U32 length,
    CoreINT_U32 capacity,
    CoreAllocatorRef bufferAllocator
)
{
    CORE_ASSERT_RET1(
        null,
        (buffer != null) || (length == 0),
        CORE_LOG_ASSERT,
        "%s(): buffer cannot be NULL when length is > 0",
        __PRETTY_FUNCTION__
    );

    return __CoreData_init(
        allocator,
        CORE_DATA_MUTABLE_BUFFER,
        buffer,
        length,
        capacity,
        (bufferAllocator == null)
            ? CoreAllocator_getDefault() : bufferAllocator
    );
}


/* CORE_PUBLIC */ CoreImmutableDataRef
CoreData_CreateImmutableWithBytesNoCopy(
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
        CORE_DATA_IMMUTABLE,
        bytes,
        length,
        length,
        (bytesDeallocator == null) 
            ? CoreAllocator_getDefault() : bytesDeallocator
    );        
}


/* CORE_PUBLIC */ CoreDataRef
CoreData_CreateCopy(
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
        CORE_DATA_MUTABLE_BUFFER,
        CoreData_getBytesPtr(data),
        length,
        maxCapacity,
        null        
    );
}


/* CORE_PUBLIC */ CoreImmutableDataRef
CoreData_CreateImmutableCopy(
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
        
    if (__CoreData_getType(data) == CORE_DATA_IMMUTABLE)
    {
        result = (CoreImmutableDataRef) Core_retain(data);
    }
    else
    {
        CoreINT_U32 length = CoreData_getLength(data);
        result = __CoreData_init(
            allocator,
            CORE_DATA_IMMUTABLE,
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
    CORE_DUMP_TRACE(me, __FUNCTION__);
    
    return __CoreData_getLength(me);
}


/* CORE_PUBLIC */ const void *
CoreData_getConstBytesPtr(CoreImmutableDataRef me)
{
    CORE_IS_DATA_RET1(me, null);
    CORE_DUMP_TRACE(me, __FUNCTION__);
    
    return __CoreData_getBytesPtr(me);
}


/* CORE_PUBLIC */ void *
CoreData_getBytesPtr(CoreImmutableDataRef me)
{
    CORE_IS_DATA_RET1(me, null);
    CORE_DUMP_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        null,
        __CoreData_getType(me) != CORE_DATA_IMMUTABLE,
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    
    return (CoreINT_U8 *) __CoreData_getBytesPtr(me);
}


/* CORE_PUBLIC */ const void *
CoreData_iterateConstBytes(CoreImmutableDataRef me, CoreINT_U32 * pIter)
{
    CORE_IS_DATA_RET1(me, null);
    CORE_DUMP_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        null,
        pIter != null,
        CORE_LOG_ASSERT,
        "%s(): pIter cannot be null-pointer", __PRETTY_FUNCTION__
    );
    
    return __CoreData_iterateConstBytes(me, pIter);
}


/* CORE_PUBLIC */ void *
CoreData_iterateBytes(CoreImmutableDataRef me, CoreINT_U32 * pIter)
{
    CORE_IS_DATA_RET1(me, null);
    CORE_DUMP_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        null,
        __CoreData_getType(me) != CORE_DATA_IMMUTABLE,
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
    
    return (void *) __CoreData_iterateConstBytes(me, pIter);
}


/* CORE_PUBLIC */ void
CoreData_copyBytes(CoreImmutableDataRef me, void * buffer)
{
    CORE_IS_DATA_RET0(me);
    CORE_DUMP_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET0(
        (buffer != null),
        CORE_LOG_ASSERT,
        "%s(): buffer cannot be null-pointer\n", __PRETTY_FUNCTION__
    );

    __CoreData_copyBytesInRange(
        me, 
        CoreRange_Create(0, __CoreData_getLength(me)),
        buffer
    );
}


/* CORE_PUBLIC */ CoreBOOL
CoreData_copyBytesInRange(CoreImmutableDataRef me, CoreRange range, void * buffer)
{
    CORE_IS_DATA_RET1(me, false);
    CORE_DUMP_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        range.offset + range.length <= __CoreData_getLength(me),
        CORE_LOG_ASSERT,
        "%s(): parameter range out of bounds\n", __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET1(
        false,
        (buffer != null) || (range.length == 0),
        CORE_LOG_ASSERT,
        "%s(): buffer cannot be null-pointer when range.length is %u > 0\n", 
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
    CORE_DUMP_TRACE(me, __FUNCTION__);
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
            int res = memcmp(meBuffer, toBuffer, min(meLength, toLength));
            result = (res < 0)
                        ? CORE_COMPARISON_LESS_THAN
                        : (res > 0)
                            ? CORE_COMPARISON_GREATER_THAN
                            : CORE_COMPARISON_EQUAL;
        }
    }
    
    return result;
}


/* CORE_PUBLIC */ CoreBOOL
CoreData_setLength(CoreDataRef me, CoreINT_U32 newLength)
{
    CORE_IS_DATA_RET1(me, false);
    CORE_DUMP_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        __CoreData_getType(me) != CORE_DATA_IMMUTABLE,
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );

    return __CoreData_accomodateLength(me, newLength);        
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
    CORE_DUMP_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        __CoreData_getType(me) != CORE_DATA_IMMUTABLE,
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    CORE_ASSERT_RET1(
        false,
        range.offset + range.length <= __CoreData_getLength(me),
        CORE_LOG_ASSERT,
        "%s(): parameter range out of bounds\n", __PRETTY_FUNCTION__
    );
        
    return _CoreData_replaceBytesInRange(me, range, null, 0);    
}


/* CORE_PUBLIC */ CoreBOOL
CoreData_appendBytes(CoreDataRef me, const void * bytes, CoreINT_U32 length)
{
    CORE_IS_DATA_RET1(me, false);
    CORE_DUMP_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        __CoreData_getType(me) != CORE_DATA_IMMUTABLE,
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
    
    return _CoreData_replaceBytesInRange(
        me, 
        CoreRange_Create(__CoreData_getLength(me), 0),
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
    CORE_DUMP_TRACE(me, __FUNCTION__);
    CORE_ASSERT_RET1(
        false,
        __CoreData_getType(me) != CORE_DATA_IMMUTABLE,
        CORE_LOG_ASSERT,
        "%s(): mutable function called on immutable object!",
        __PRETTY_FUNCTION__
    );
    
    return _CoreData_replaceBytesInRange(me, range, bytes, length);    
}


 