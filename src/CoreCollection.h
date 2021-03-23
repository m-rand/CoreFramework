
/*********************************************************************
	Author			: Marcel Balas
	Name			: CoreCollection
	Generated Date	: 2009-06-17  
*********************************************************************/

/*****************************************************************************
*
*
*
*****************************************************************************/

#ifndef CoreCollection_H 

#define CoreCollection_H 


#include <CoreFramework/CoreBase.h>
#include "CoreInternal.h"




/*****************************************************************************
*
* Type definitions
* 
*****************************************************************************/

typedef struct __CoreCollection * CoreCollectionRef;

typedef const struct __CoreCollection * CoreImmutableCollectionRef;


typedef const void *(* CoreCollectionRetainCallback) (const void * value);
typedef void (* CoreCollectionReleaseCallback) (const void * value);
typedef char * (* CoreCollectionGetCopyOfDescriptionCallback) (const void * value);
typedef CoreBOOL (* CoreCollectionEqualCallback) (const void * v1, const void * v2);
typedef CoreHashCode (* CoreCollectionHashCallback) (const void * value);

typedef struct
{
	CoreCollectionRetainCallback retain;
	CoreCollectionReleaseCallback release;
	CoreCollectionGetCopyOfDescriptionCallback getCopyOfDescription;
	CoreCollectionEqualCallback equal;
	CoreCollectionHashCallback hash;
} CoreCollectionValueCallbacks;



CORE_PUBLIC const CoreCollectionValueCallbacks CoreCollectionValueCoreCallbacks;
CORE_PUBLIC const CoreCollectionValueCallbacks CoreCollectionValueNullCallbacks;





CORE_PROTECTED void
CoreCollection_initialize(void);
 
 
 
 

CORE_PUBLIC CoreCollectionRef
CoreCollection_create(
    CoreAllocatorRef allocator,
    CoreINT_U32 capacity,
    const CoreCollectionValueCallbacks * valueCallbacks
);

CORE_PUBLIC CoreCollectionRef
CoreCollection_createImmutable(
    CoreAllocatorRef allocator,
    const void ** values,
    CoreINT_U32 count,
    const CoreCollectionValueCallbacks * valueCallbacks
);

CORE_PUBLIC CoreCollectionRef
CoreCollection_createCopy(
    CoreAllocatorRef allocator,
    CoreImmutableCollectionRef dictionary,
    CoreINT_U32 capacity);

CORE_PUBLIC CoreImmutableCollectionRef
CoreCollection_createImmutableCopy(
    CoreAllocatorRef allocator,
    CoreImmutableCollectionRef dictionary);


CORE_PUBLIC CoreINT_U32
CoreCollection_getCount(CoreImmutableCollectionRef me);

CORE_PUBLIC CoreINT_U32
CoreCollection_getCountOfValue(CoreImmutableCollectionRef me, const void * value);

CORE_PUBLIC const void *
CoreCollection_getValue(CoreImmutableCollectionRef me, const void * value);

CORE_PUBLIC CoreBOOL
CoreCollection_getValueIfPresent(
    CoreImmutableCollectionRef me, 
    const void * candidate,
    void ** value
);

CORE_PUBLIC CoreBOOL
CoreCollection_containsValue(CoreImmutableCollectionRef me, const void * value);

CORE_PUBLIC void
CoreCollection_copyValues(
    CoreImmutableCollectionRef me,
    void ** values);

CORE_PROTECTED CoreINT_U32
_CoreCollection_iterate(
    CoreImmutableCollectionRef me, 
    __CoreIteratorState * state,
    void * buffer,
    CoreINT_U32 count
);

CORE_PUBLIC CoreBOOL
CoreCollection_addValue(
    CoreCollectionRef me, 
    const void * value
);

CORE_PUBLIC CoreBOOL
CoreCollection_removeValue(
    CoreCollectionRef me, 
    const void * value
);

CORE_PUBLIC CoreBOOL
CoreCollection_replaceValue(
    CoreCollectionRef me, 
    const void * value
);

CORE_PUBLIC void
CoreCollection_clear(CoreCollectionRef me);


typedef void (* CoreCollectionApplyFunction)(const void * value, void * context);

CORE_PUBLIC void
CoreCollection_applyFunction(
    CoreImmutableCollectionRef me,
    CoreCollectionApplyFunction map,
    void * context    
);


char *
_CoreCollection_copyDescription(CoreImmutableCollectionRef me);

CORE_PROTECTED char * 
_CoreCollection_description(CoreImmutableCollectionRef me);

#endif

