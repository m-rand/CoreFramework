
/*********************************************************************
	Author			: Marcel Balas
	Name			: CoreDictionary
	Generated Date	: 2009-06-17  
*********************************************************************/

/*****************************************************************************
*
*
*
*****************************************************************************/

#ifndef CoreDictionary_H 

#define CoreDictionary_H 


#include <CoreFramework/CoreBase.h>
#include "CoreInternal.h"




/*****************************************************************************
*
* Type definitions
* 
*****************************************************************************/

typedef struct __CoreDictionary * CoreDictionaryRef;

typedef const struct __CoreDictionary * CoreImmutableDictionaryRef;


typedef const void *(* CoreDictionaryRetainCallback) (const void * value);
typedef void (* CoreDictionaryReleaseCallback) (const void * value);
typedef char * (* CoreDictionaryGetCopyOfDescriptionCallback) (const void * value);
typedef CoreBOOL (* CoreDictionary_equalCallback) (const void * v1, const void * v2);
typedef CoreHashCode (* CoreDictionary_hashCallback) (const void * key);

typedef struct
{
	Core_retainInfoCallback retain;
	Core_releaseInfoCallback release;
	Core_getCopyOfDescriptionCallback getCopyOfDescription;
	CoreDictionary_equalCallback equal;
	CoreDictionary_hashCallback hash;
} CoreDictionaryKeyCallbacks;

typedef struct
{
	Core_retainInfoCallback retain;
	Core_releaseInfoCallback release;
	Core_getCopyOfDescriptionCallback getCopyOfDescription;
	CoreDictionary_equalCallback equal;
} CoreDictionaryValueCallbacks;


CORE_PUBLIC const CoreDictionaryKeyCallbacks CoreDictionaryKeyCoreCallbacks;
CORE_PUBLIC const CoreDictionaryKeyCallbacks CoreDictionaryKeyNullCallbacks;

CORE_PUBLIC const CoreDictionaryValueCallbacks CoreDictionaryValueCoreCallbacks;
CORE_PUBLIC const CoreDictionaryValueCallbacks CoreDictionaryValueNullCallbacks;





CORE_PROTECTED void
CoreDictionary_initialize(void);
 
CORE_PUBLIC CoreClassID
CoreDictionary_getClassID(void);
 
 
 

CORE_PUBLIC CoreDictionaryRef
CoreDictionary_create(
    CoreAllocatorRef allocator,
    CoreINT_U32 capacity,
    const CoreDictionaryKeyCallbacks * keyCallbacks,
    const CoreDictionaryValueCallbacks * valueCallbacks
);

CORE_PUBLIC CoreDictionaryRef
CoreDictionary_createImmutable(
    CoreAllocatorRef allocator,
    const void ** keys,
    const void ** values,
    CoreINT_U32 count,
    const CoreDictionaryKeyCallbacks * keyCallbacks,
    const CoreDictionaryValueCallbacks * valueCallbacks
);

CORE_PUBLIC CoreDictionaryRef
CoreDictionary_createCopy(
    CoreAllocatorRef allocator,
    CoreImmutableDictionaryRef dictionary,
    CoreINT_U32 capacity);

CORE_PUBLIC CoreImmutableDictionaryRef
CoreDictionary_createImmutableCopy(
    CoreAllocatorRef allocator,
    CoreImmutableDictionaryRef dictionary);


CORE_PUBLIC CoreINT_U32
CoreDictionary_getCount(CoreImmutableDictionaryRef me);

CORE_PUBLIC CoreINT_U32 
CoreDictionary_getCountOfValue(
    CoreImmutableDictionaryRef me,
    const void * value
);

CORE_PUBLIC const void *
CoreDictionary_getValue(CoreImmutableDictionaryRef me, const void * key);

CORE_PUBLIC CoreBOOL
CoreDictionary_getValueIfPresent(
    CoreImmutableDictionaryRef me,
    const void * key,
    const void ** value    
);

CORE_PUBLIC CoreBOOL
CoreDictionary_containsKey(CoreImmutableDictionaryRef me, const void * key);

CORE_PUBLIC CoreBOOL
CoreDictionary_containsValue(CoreImmutableDictionaryRef me, const void * value);

CORE_PUBLIC void
CoreDictionary_copyKeysAndValues(
    CoreImmutableDictionaryRef me,
    const void ** keys,
    const void ** values
);

CORE_PROTECTED CoreINT_U32
_CoreDictionary_iterate(
    CoreImmutableDictionaryRef me, 
    __CoreIteratorState * state,
    const void ** buffer,
    CoreINT_U32 count
);

CORE_PUBLIC CoreBOOL
CoreDictionary_addValue(
    CoreDictionaryRef me, 
    const void * key,
    const void * value
);

CORE_PUBLIC CoreBOOL
CoreDictionary_removeValue(
    CoreDictionaryRef me, 
    const void * key
);

CORE_PUBLIC CoreBOOL
CoreDictionary_replaceValue(
    CoreDictionaryRef me, 
    const void * key,
    const void * value
);

CORE_PUBLIC void
CoreDictionary_clear(CoreDictionaryRef me);


typedef void (* CoreDictionaryApplyFunction) (
    const void * key,
    const void * value,
    void * context
);

CORE_PUBLIC void
CoreDictionary_applyFunction(
    CoreImmutableDictionaryRef me,
    CoreDictionaryApplyFunction map,
    void * context    
);


char *
_CoreDictionary_copyDescription(CoreImmutableDictionaryRef me);

CORE_PROTECTED char * 
_CoreDictionary_description(CoreImmutableDictionaryRef me);

#endif

