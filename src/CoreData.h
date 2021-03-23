
/*********************************************************************
	Author			: Marcel Balas
	Name			: CoreData
	Generated Date	: 2009-04-29  
*********************************************************************/

/*****************************************************************************
*
*	CoreData is an object oriented wrapper for byte buffer. 
*
*
*****************************************************************************/

#ifndef CoreData_H 

#define CoreData_H 


#include <CoreFramework/CoreBase.h>




/*****************************************************************************
*
* Type definitions
* 
*****************************************************************************/

typedef struct __CoreData * CoreDataRef;

typedef const struct __CoreData * CoreImmutableDataRef;



CORE_PROTECTED void
CoreData_initialize(void);
 

CORE_PUBLIC CoreDataRef
CoreData_create(CoreAllocatorRef allocator, CoreINT_U32 maxCapacity);


CORE_PUBLIC CoreImmutableDataRef
CoreData_createImmutable(
    CoreAllocatorRef allocator,
    const void * bytes,
    CoreINT_U32 length
);


CORE_PUBLIC CoreDataRef
CoreData_createWithExternalBufferNoCopy(
    CoreAllocatorRef allocator,
    const void * buffer,
    CoreINT_U32 length,
    CoreINT_U32 capacity,
    CoreAllocatorRef bufferAllocator
);


CORE_PUBLIC CoreImmutableDataRef
CoreData_createImmutableWithBytesNoCopy(
    CoreAllocatorRef allocator,
    const void * bytes,
    CoreINT_U32 length,
    CoreAllocatorRef bytesDeallocator
);


// Copy constructor
CORE_PUBLIC CoreDataRef 
CoreData_createCopy(
    CoreAllocatorRef allocator,
    CoreINT_U32 maxCapacity,
    CoreImmutableDataRef data
);


// Copy constructor -- immutable
CORE_PUBLIC CoreImmutableDataRef 
CoreData_createImmutableCopy(
    CoreAllocatorRef allocator,
    CoreImmutableDataRef data
);


CORE_PUBLIC CoreBOOL 
CoreData_append(CoreDataRef me, CoreImmutableDataRef data);


CORE_PUBLIC CoreBOOL 
CoreData_appendBytes(CoreDataRef me, const void * buffer, CoreINT_U32 length);


CORE_PUBLIC CoreBOOL 
CoreData_removeBytesInRange(CoreDataRef me, CoreRange range);


CORE_PUBLIC CoreBOOL
CoreData_insertBytes(
    CoreDataRef me, 
    CoreINT_U32 index,
    const void * bytes, 
    CoreINT_U32 length
);


CORE_PUBLIC CoreBOOL 
CoreData_replaceBytesInRange(
	CoreDataRef me, CoreRange range, 
	const void * buffer, CoreINT_U32 length
);


CORE_PUBLIC CoreBOOL
CoreData_setLength(CoreDataRef me, CoreINT_U32 length);


CORE_PUBLIC CoreComparison
CoreData_compare(CoreImmutableDataRef me, CoreImmutableDataRef to);


CORE_PUBLIC void 
CoreData_copyBytes(CoreImmutableDataRef me, void * buffer);


CORE_PUBLIC CoreBOOL 
CoreData_copyBytesInRange(CoreImmutableDataRef me, CoreRange range, void * buffer);


CORE_PUBLIC CoreINT_U32 
CoreData_getLength(CoreImmutableDataRef me);


CORE_PUBLIC CoreINT_U8 * 
CoreData_getBytesPtr(CoreImmutableDataRef me);


CORE_PUBLIC CoreINT_U8 * 
CoreData_iterateBytes(CoreImmutableDataRef me, CoreINT_U32 * iter);


CORE_PUBLIC const CoreINT_U8 * 
CoreData_getConstBytesPtr(CoreImmutableDataRef me);


CORE_PUBLIC const CoreINT_U8 * 
CoreData_iterateConstBytes(CoreImmutableDataRef me, CoreINT_U32 * iter);



/******************************************************************************
*
* Clears the deque's content.
*
* Params:
*	INOUT	CoreDataRef	me	this object
*
* Returns:	void
*
*
******************************************************************************/
CORE_PUBLIC void CoreData_clear(CoreDataRef me);


CORE_PUBLIC CoreBOOL
CoreData_setLength(CoreDataRef me, CoreINT_U32 newLength);


CORE_PUBLIC void
CoreData_setExternalBytesNoCopy(
    CoreDataRef me,
    const void * bytes,
    CoreINT_U32 length,
    CoreINT_U32 capacity 
);



/******************************************************************************
 *
 * CoreDataIterator
 * 
 *****************************************************************************/   

typedef struct CoreDataIterator
{
	CoreImmutableDataRef theData;
	const CoreINT_U8 * buffer;
	CoreRange neededRange;
	CoreINT_U32 cachedLength;
	CoreINT_U32 iterator;
} CoreDataIterator;


#if defined(CORE_INLINE)

CORE_INLINE void
CoreDataIterator_initWithData(
	CoreDataIterator * me,
	CoreImmutableDataRef data,
	CoreRange range
)
{
	me->theData = data;
	me->neededRange = range;
	me->iterator = range.offset;
	me->cachedLength = range.offset;
	me->buffer = CoreData_iterateConstBytes(
		data, 
		&me->cachedLength
	);
}

CORE_INLINE void
CoreDataIterator_initWithBytes(
	CoreDataIterator * me,
	const CoreINT_U8 * bytes,
	CoreINT_U32 length
)
{
	me->buffer = bytes;
	me->neededRange.offset = 0;
	me->neededRange.length = length;
	me->iterator = 0;
	me->cachedLength = length;
	me->theData = null;
}

CORE_INLINE const CoreINT_U8
CoreDataIterator_getNextByte(CoreDataIterator * me)
{
	CoreINT_U8 result = 0;
	
	if (me->iterator < me->cachedLength)
	{
		result = me->buffer[me->iterator++];
	}
	else 
	{
		if (me->iterator < me->neededRange.length)
		{
			CoreINT_U32 tmp = me->iterator;
			
			me->buffer = CoreData_iterateConstBytes(
				me->theData, 
				&tmp
			);
			me->cachedLength = me->iterator + tmp;
			result = me->buffer[me->iterator++]; 
		}
	}

	return (const CoreINT_U8) result;
}

#else /* defined(CORE_INLINE) */

#define CoreDataIterator_initWithData(me, data, range) \
    __CoreDataIterator_initWithData(me, data, range)

#define CoreDataIterator_initWithBytes(me, bytes, length) \
    __CoreDataIterator_initWithBytes(me, bytes, length)

#define CoreDataIterator_getNextByte(me) __CoreDataIterator_getNextByte(me)

#endif /* defined(CORE_INLINE) */



#endif


/******************************************************************************
 *
 *	History:
 *
 *	version		datum	\	descriptionon
 *	---------------------------------------------------------------------------
 *
 *	v 01.00		2009-04-29
 *		Initial version
 *
 *
 ******************************************************************************/		
			
