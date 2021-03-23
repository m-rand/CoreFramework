
#ifndef CoreBase_H 

#define CoreBase_H 

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>






/*****************************************************************************
 *
 *  CORE ARCHITECTURE DEFINITIONS
 *  
 *****************************************************************************/

#if (defined(__CYGWIN32__) || defined(_WIN32)) && !defined(__WIN32__)
#define __WIN32__ 1
#elif (defined(UCLINUX) || defined(LINUX)) && !defined(__LINUX__)
#define __LINUX__ 1
#endif


#if defined(__WIN32__)
#include "windows.h"
#include "Winbase.h"
#elif defined(__LINUX__)
#include <atomic.h>
#endif


#if defined(_DEBUG) && !defined(DEBUG)
#define DEBUG 1
#endif



/*****************************************************************************
 *
 *  CORE BASIC TYPES AND DEFINES
 *  
 *****************************************************************************/
 
typedef unsigned int        CoreBOOL;
typedef char                CoreCHAR_8;
typedef unsigned short int  CoreUniChar;
typedef signed short int    CoreINT_S16;
typedef long int            CoreINT_S32;
typedef long long int       CoreINT_S64;
typedef signed char         CoreINT_S8;
typedef unsigned short int  CoreINT_U16;
typedef unsigned long       CoreINT_U32;
typedef unsigned long long  CoreINT_U64;
typedef unsigned char       CoreINT_U8;
typedef float               CoreREAL_32;
typedef double              CoreREAL_64;
typedef void *              CoreRef;
typedef const void *        CoreConstRef;
typedef CoreINT_U32         CoreIndex;
typedef CoreINT_U32         CoreHashCode;
typedef const void *        CoreObjectRef; // a generic object

#if !defined(false)
    #define false 0
#endif

#if !defined(true)
    #define true 1
#endif

#define CoreINT_S8_MAX   0x7f
#define CoreINT_S8_MIN   (-CoreINT_8_MAX - 1)
#define CoreINT_U8_MAX  0xff

#define CoreINT_S16_MAX  0x7fff
#define CoreINT_S16_MIN  (-CoreINT_16_MAX - 1)
#define CoreINT_U16_MAX 0xffffU

#define CoreINT_S32_MAX  0x7fffffff
#define CoreINT_S32_MIN  (-CoreINT_32_MAX - 1)
#define CoreINT_U32_MAX 0xffffffffUL

#define CoreINT_S64_MAX  0x7fffffffffffffffLL
#define CoreINT_S64_MIN  (-CoreINT_64_MAX - 1)
#define CoreINT_U64_MAX 0xffffffffffffffff

#define CORE_INDEX_NOT_FOUND CoreINT_U32_MAX

typedef enum CoreComparison 
{
    CORE_COMPARISON_LESS_THAN = -1,
    CORE_COMPARISON_EQUAL = 0,
    CORE_COMPARISON_GREATER_THAN = 1,
    CORE_COMPARISON_UNCOMPARABLE = CoreINT_U32_MAX
} CoreComparison;


#define null NULL


typedef CoreComparison (* CoreComparatorFunction) (const void * a, const void * b);



/*****************************************************************************
 *
 *  CORE ACCESS RIGHTS
 *  
 *****************************************************************************/

#if !defined(CORE_INLINE)
    #if defined(__GNUC__) && (__GNUC__ == 4) && !defined(DEBUG)
        #define CORE_INLINE static __inline__ __attribute__((always_inline))
    #elif defined(__GNUC__)
        #define CORE_INLINE static __inline__
    #elif defined(__WIN32__)
        #define CORE_INLINE static __inline
    #endif
#endif

#ifdef CORE_PROTECTED
    #undef CORE_PROTECTED
#endif
#define CORE_PROTECTED extern

#ifdef CORE_PUBLIC
    #undef CORE_PUBLIC
#endif
#define CORE_PUBLIC extern


#ifndef CORE_FINAL
    #define CORE_FINAL 
#endif




/*****************************************************************************
 *
 *  LOGGING MECHANISMS PUBLIC API
 *  
 *****************************************************************************/
    
extern void
Core_setLogInfo(FILE * logFile, CoreINT_U32 logLevel);

extern CoreINT_U32
Core_getLogLevel(void);

extern FILE * 
Core_getLogFile(void);


#define CORE_LOG_TRACE        (1 << 0)    /* all (public) methods */
#define CORE_LOG_CRITICAL     (1 << 1)    /* CoreFramework critical errors */
#define CORE_LOG_ASSERT       (1 << 2)    /* user param checks */
#define CORE_LOG_INFO         (1 << 3)    /* additional informations */
#define CORE_LOG_FULL         ( \
            CORE_LOG_TRACE  | CORE_LOG_CRITICAL | \
            CORE_LOG_ASSERT | CORE_LOG_INFO \
        )         



/*****************************************************************************
 *
 *  CoreRange
 *  
 *****************************************************************************/

typedef struct CoreRange 
{
    CoreINT_U32 offset;		
    CoreINT_U32 length;		
} CoreRange;

#if defined(CORE_INLINE)
CORE_INLINE CoreRange 
CoreRange_create(CoreINT_U32 offset, CoreINT_U32 length)
{
    CoreRange range = { offset, length };
    return range;
}

CORE_INLINE CoreRange
CoreRange_make(CoreINT_U32 offset, CoreINT_U32 length)
{
    CoreRange range = { offset, length };
    return range;
}
#else
#define CoreRange_create(offset, length) __CoreRange_create(offset, length)
#define CoreRange_make(offset, length) __CoreRange_create(offset, length)
#endif



/*****************************************************************************
 *
 *  CoreNull
 *  
 *****************************************************************************/

/*
typedef const struct * __CoreNull CoreNullRef

CORE_PUBLIC CoreClassID
CoreNull_getClassID(void);

CORE_PUBLIC const CoreNullRef CORE_NULL; // the singleton
*/



/*****************************************************************************
 *
 *  CoreAllocator
 *  
 *****************************************************************************/

typedef struct __CoreString * CoreStringRef;

typedef const struct __CoreString * CoreImmutableStringRef;


typedef const struct __CoreAllocator * CoreAllocatorRef;

typedef const void * (* Core_retainInfoCallback) (const void *);
typedef void (* Core_releaseInfoCallback) (const void *);
typedef CoreImmutableStringRef (* Core_getCopyOfDescriptionCallback) (const void *);
typedef void * (* CoreAllocator_allocateCallback) (CoreINT_U32, const void *);
typedef void * (* CoreAllocator_reallocateCallback) (void *, CoreINT_U32, const void *);
typedef void (* CoreAllocator_deallocateCallback) (void *, const void *);

typedef struct CoreAllocatorDelegate
{
    void *                              info;
    Core_retainInfoCallback             retainInfo;
    Core_releaseInfoCallback            releaseInfo;
    Core_getCopyOfDescriptionCallback   getCopyOfDescription;
    CoreAllocator_allocateCallback      allocate;
    CoreAllocator_reallocateCallback    reallocate;
    CoreAllocator_deallocateCallback    deallocate;
} CoreAllocatorDelegate;


CORE_PUBLIC CoreAllocatorRef
CoreAllocator_getDefault(void);

CORE_PUBLIC void
CoreAllocator_setDefault(CoreAllocatorRef allocator);

CORE_PUBLIC CoreAllocatorRef
CoreAllocator_create(
    CoreAllocatorRef allocator, 
    CoreAllocatorDelegate * delegate,
    CoreBOOL useDelegate
);

CORE_PUBLIC void *
CoreAllocator_allocate(CoreAllocatorRef me, CoreINT_U32 size);

CORE_PUBLIC void *
CoreAllocator_reallocate(
    CoreAllocatorRef me, 
    void * memPtr,
    CoreINT_U32 newSize
);

CORE_PUBLIC void
CoreAllocator_deallocate(CoreAllocatorRef me, void * memPtr);

CORE_PUBLIC void
CoreAllocator_copyAllocatorDelegate(
    CoreAllocatorRef me, 
    CoreAllocatorDelegate * delegate
);

CORE_PROTECTED void
CoreAllocator_initialize(void);


extern const CoreAllocatorRef CORE_ALLOCATOR_SYSTEM;
extern const CoreAllocatorRef CORE_ALLOCATOR_EMPTY;





/*****************************************************************************
 *
 *  CoreBase initialization
 *  
 *****************************************************************************/

CORE_PROTECTED void
CoreBase_initialize(void);





/*****************************************************************************
 *
 *  Core initialization
 *  
 *****************************************************************************/

CORE_PUBLIC CoreBOOL
Core_initialize(void);





/*****************************************************************************
 *
 *  CoreFramework common polymorphic methods.
 *  
 *****************************************************************************/


typedef CoreINT_U32 CoreClassID;

CORE_PUBLIC CoreINT_U32
Core_getRetainCount(CoreObjectRef me);

CORE_PUBLIC CoreObjectRef
Core_retain(CoreObjectRef o);

CORE_PUBLIC void
Core_release(CoreObjectRef o);

CORE_PUBLIC CoreHashCode
Core_hash(CoreObjectRef me);

CORE_PUBLIC CoreBOOL
Core_equal(CoreObjectRef me, CoreObjectRef to);

CORE_PUBLIC CoreImmutableStringRef
Core_getCopyOfDescription(CoreObjectRef me);

CORE_PUBLIC CoreClassID
Core_getClassID(CoreObjectRef o);

CORE_PUBLIC CoreAllocatorRef
Core_getAllocator(CoreObjectRef me);


#endif  

