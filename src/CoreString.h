/*********************************************************************
	Author			: Marcel Balas
	Name			: CoreString
	Generated Date	: 2009-07-08  
*********************************************************************/

/*****************************************************************************
*
*	CoreString is an object oriented wrapper for string. 
*
*
*****************************************************************************/

#ifndef CoreString_H 

#define CoreString_H 


#include <CoreFramework/CoreBase.h>



/*****************************************************************************
*
* Type definitions
* 
*****************************************************************************/

// see CoreBase.h

extern CoreImmutableStringRef CORE_EMPTY_STRING;


CORE_PUBLIC CoreImmutableStringRef
CoreString_createImmutableWithASCII(
    CoreAllocatorRef allocator,
    const CoreCHAR_8 * characters,
    CoreINT_U32 length
);

CORE_PROTECTED CoreStringRef
CoreString_create(CoreAllocatorRef allocator, CoreINT_U32 capacity);

CORE_PUBLIC CoreImmutableStringRef
CoreString_createImmutableCopy(
    CoreAllocatorRef allocator,
    CoreImmutableStringRef str
);

CORE_PUBLIC CoreINT_U32 
CoreString_getLength(CoreImmutableStringRef me);


CORE_PUBLIC const CoreCHAR_8 * 
CoreString_getConstASCIICharactersPtr(CoreImmutableStringRef me);

CORE_PUBLIC CoreComparison
CoreString_compare(CoreImmutableStringRef me, CoreImmutableStringRef to);


CORE_PUBLIC CoreBOOL
CoreString_appendASCIICharacters(
    CoreStringRef me,
    const CoreCHAR_8 * characters,
    CoreINT_U32 length
);

CORE_PUBLIC CoreClassID
CoreString_getClassID(void);


CORE_PROTECTED void
CoreString_initialize(void);

#endif
