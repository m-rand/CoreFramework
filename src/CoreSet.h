
/*********************************************************************
	Author			: Marcel Balas
	Name			: CoreSet
	Generated Date	: 2009-06-17  
*********************************************************************/

/*****************************************************************************
*
*
*
*****************************************************************************/

#ifndef CoreSet_H 

#define CoreSet_H 


#include <CoreFramework/CoreBase.h>
#include "CoreInternal.h"




/*****************************************************************************
*
* Type definitions
* 
*****************************************************************************/

typedef struct __CoreSet * CoreSetRef;

typedef const struct __CoreSet * CoreImmutableSetRef;


typedef const void *(* CoreSetRetainCallback) (const void * value);
typedef void (* CoreSetReleaseCallback) (const void * value);
typedef char * (* CoreSetGetCopyOfDescriptionCallback) (const void * value);
typedef CoreBOOL (* CoreSet_equalCallback) (const void * v1, const void * v2);
typedef CoreHashCode (* CoreSet_hashCallback) (const void * value);

typedef struct
{
	Core_retainInfoCallback retain;
	Core_releaseInfoCallback release;
	Core_getCopyOfDescriptionCallback getCopyOfDescription;
	CoreSet_equalCallback equal;
	CoreSet_hashCallback hash;
} CoreSetValueCallbacks;



CORE_PUBLIC const CoreSetValueCallbacks CoreSetValueCoreCallbacks;
CORE_PUBLIC const CoreSetValueCallbacks CoreSetValueNullCallbacks;





CORE_PROTECTED void
CoreSet_initialize(void);
 
 
 
 

CORE_PUBLIC CoreSetRef
CoreSet_create(
    CoreAllocatorRef allocator,
    CoreINT_U32 capacity,
    const CoreSetValueCallbacks * valueCallbacks
);

CORE_PUBLIC CoreSetRef
CoreSet_createImmutable(
    CoreAllocatorRef allocator,
    const void ** values,
    CoreINT_U32 count,
    const CoreSetValueCallbacks * valueCallbacks
);

CORE_PUBLIC CoreSetRef
CoreSet_createCopy(
    CoreAllocatorRef allocator,
    CoreImmutableSetRef dictionary,
    CoreINT_U32 capacity);

CORE_PUBLIC CoreImmutableSetRef
CoreSet_createImmutableCopy(
    CoreAllocatorRef allocator,
    CoreImmutableSetRef dictionary);


CORE_PUBLIC CoreINT_U32
CoreSet_getCount(CoreImmutableSetRef me);

CORE_PUBLIC const void *
CoreSet_getValue(CoreImmutableSetRef me, const void * value);

CORE_PUBLIC CoreBOOL
CoreSet_getValueIfPresent(
    CoreImmutableSetRef me, 
    const void * candidate,
    const void ** value
);

CORE_PUBLIC CoreBOOL
CoreSet_containsValue(CoreImmutableSetRef me, const void * value);

CORE_PUBLIC void
CoreSet_copyValues(
    CoreImmutableSetRef me,
    const void ** values);

CORE_PROTECTED CoreINT_U32
_CoreSet_iterate(
    CoreImmutableSetRef me, 
    __CoreIteratorState * state,
    const void ** buffer,
    CoreINT_U32 count
);

CORE_PUBLIC CoreBOOL
CoreSet_addValue(
    CoreSetRef me, 
    const void * value
);

CORE_PUBLIC CoreBOOL
CoreSet_removeValue(
    CoreSetRef me, 
    const void * value
);

CORE_PUBLIC CoreBOOL
CoreSet_replaceValue(
    CoreSetRef me, 
    const void * value
);

CORE_PUBLIC void
CoreSet_clear(CoreSetRef me);

typedef void (* CoreSetApplyFunction)(const void * value, void * context);

CORE_PUBLIC void
CoreSet_applyFunction(
    CoreImmutableSetRef me,
    CoreSetApplyFunction map,
    void * context    
);

char *
_CoreSet_copyDescription(CoreImmutableSetRef me);

CORE_PROTECTED char * 
_CoreSet_description(CoreImmutableSetRef me);

#endif

