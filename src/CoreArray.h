
/*********************************************************************
	Author			: Marcel Balas
	Name			: CoreArray
	Generated Date	: 2009-04-29  
*********************************************************************/

/*****************************************************************************
*
*
*
*****************************************************************************/

#ifndef CoreArray_H 

#define CoreArray_H 


#include <CoreFramework/CoreBase.h>
#include "CoreInternal.h"




/*****************************************************************************
*
* Type definitions
* 
*****************************************************************************/

typedef struct __CoreArray * CoreArrayRef;

typedef const struct __CoreArray * CoreImmutableArrayRef;


/*
typedef const void *(* CoreArrayRetainCallback) (
    //CoreAllocatorRef allocator,
    const void * value
);
typedef void (* CoreArrayReleaseCallback) (
    //CoreAllocatorRef allocator,
    const void * value
);
typedef char * (* CoreArrayGetCopyOfDescriptionCallback) (const void * value);
*/
typedef CoreBOOL (* CoreArray_equalCallback) (const void * v1, const void * v2);

typedef struct
{
	Core_retainInfoCallback retain;
	Core_releaseInfoCallback release;
	Core_getCopyOfDescriptionCallback getCopyOfDescription;
	CoreArray_equalCallback equal;
} CoreArrayCallbacks;

CORE_PUBLIC const CoreArrayCallbacks CoreArrayCoreCallbacks;
CORE_PUBLIC const CoreArrayCallbacks CoreArrayNullCallbacks;





CORE_PROTECTED void
CoreArray_initialize(void);
 
CORE_PUBLIC CoreClassID
CoreArray_getClassID(void);

 
 

CORE_PUBLIC CoreArrayRef
CoreArray_create(
    CoreAllocatorRef allocator,
    CoreINT_U32 capacity,
    const CoreArrayCallbacks * callbacks
);

CORE_PUBLIC CoreImmutableArrayRef
CoreArray_createImmutable(
    CoreAllocatorRef allocator,
    const void ** values,
    CoreINT_U32 count,
    const CoreArrayCallbacks * callbacks
);

CORE_PUBLIC CoreArrayRef
CoreArray_createCopy(
    CoreAllocatorRef allocator,
    CoreImmutableArrayRef array,
    CoreINT_U32 capacity
);

CORE_PUBLIC CoreImmutableArrayRef
CoreArray_createImmutableCopy(
    CoreAllocatorRef allocator,
    CoreImmutableArrayRef array
);

CORE_PUBLIC CoreINT_U32
CoreArray_getCount(CoreImmutableArrayRef me);

CORE_PUBLIC CoreINT_U32
CoreArray_getCountOfValue(CoreImmutableArrayRef me, const void * value);

CORE_PUBLIC const void *
CoreArray_getValueAtIndex(CoreImmutableArrayRef me, CoreINT_U32 index);

CORE_PUBLIC CoreINT_U32
CoreArray_getFirstIndexOfValue(
    CoreImmutableArrayRef me, 
    CoreRange range,
    const void * value
);

CORE_PUBLIC CoreINT_U32
CoreArray_getLastIndexOfValue(
    CoreImmutableArrayRef me, 
    CoreRange range,
    const void * value
);

CORE_PUBLIC void
CoreArray_copyValues(
    CoreImmutableArrayRef me, 
    CoreRange range, 
    void ** values
);

CORE_PROTECTED CoreINT_U32
_CoreArray_iterate(
    CoreImmutableArrayRef me, 
    __CoreIteratorState * state,
    const void ** buffer,
    CoreINT_U32 count
);

CORE_PUBLIC CoreBOOL
CoreArray_addValue(CoreArrayRef me, const void * value);

CORE_PUBLIC CoreBOOL
CoreArray_insertValueAtIndex(
    CoreArrayRef me, 
    CoreINT_U32 index, 
    const void * value
);

CORE_PUBLIC const void *
CoreArray_setValueAtIndex(
    CoreArrayRef me, 
    CoreINT_U32 index, 
    const void * value
);

CORE_PUBLIC CoreBOOL
CoreArray_removeValueAtIndex(
    CoreArrayRef me, 
    CoreINT_U32 index
);

CORE_PUBLIC CoreBOOL
CoreArray_replaceValues(
    CoreArrayRef me,
    CoreRange range,
    const void ** values,
    CoreINT_U32 count    
);

CORE_PUBLIC void CoreArray_clear(CoreArrayRef me);

typedef void (* CoreArrayApplyFunction)(const void * value, void * context);

CORE_PUBLIC void
CoreArray_applyFunction(
    CoreImmutableArrayRef me,
    CoreRange range,
    CoreArrayApplyFunction map,
    void * context    
);

CORE_PUBLIC void
CoreArray_sortValues(
    CoreArrayRef me,
    CoreRange range,
    CoreComparatorFunction cmp
);

char *
_CoreArray_copyDescription(CoreImmutableArrayRef me);

#endif

