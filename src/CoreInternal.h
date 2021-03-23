
#ifndef CoreInternal_H 

#define CoreInternal_H 


#include <stdio.h>
#include <CoreFramework/CoreBase.h>
#include "CoreRuntime.h"

#if defined(__LINUX__)
#include <stdarg.h>
#include <time.h>
#elif defined(__WIN32__)
#include <windows.h>
#endif


/*****************************************************************************
 *
 *  COMMONLY USED DEFINITIONS
 *  
 *****************************************************************************/

#if defined(__LINUX__)
    #ifndef min
        #define min(x,y) ({ \
            const typeof(x) _x = (x);       \
            const typeof(y) _y = (y);       \
            _x < _y ? _x : _y; })
    #endif
#elif __WIN32__
    #ifndef min
        #define min(x,y) ((x) < (y) ? (x) : (y))
    #endif
#endif

#if defined(__LINUX__)
    #ifndef max
        #define max(x,y) ({ \
            const typeof(x) _x = (x);       \
            const typeof(y) _y = (y);       \
            _x > _y ? _x : _y; })
    #endif
#elif __WIN32__
    #ifndef max
        #define max(x,y) ((x) > (y) ? (x) : (y))
    #endif
#endif


#ifdef __GNUC__
    #define CORE_LIKELY(x)      __builtin_expect((!!(x)),1)
    #define CORE_UNLIKELY(x)    __builtin_expect((!!(x)),0)
#elif __WIN32__
    #define CORE_LIKELY(x)      (x)
    #define CORE_UNLIKELY(x)    (x)
#endif



#if defined(__WIN32__) && defined(_MSC_VER)
#define __PRETTY_FUNCTION__ __FUNCSIG__
/*#elif defined(__LINUX__)
#define __FUNCTION__ __FUNC__*/
#endif



/*****************************************************************************
 *
 *  CoreAtomic
 *  
 *****************************************************************************/

#if defined(__WIN32__)
#define CORE_ATOMIC_COMPARE_AND_SWAP_32(mem, newVal, oldVal) \
    (InterlockedCompareExchange(mem, newVal, oldVal) == (oldVal))


#define CORE_ATOMIC_COMPARE_AND_SWAP_PTR(mem, newVal, oldVal) \
    (InterlockedCompareExchangePointer(mem, newVal, oldVal) == (oldVal))


#define CORE_ATOMIC_INCREMENT(mem) InterlockedIncrement(mem)
#define CORE_ATOMIC_DECREMENT(mem) InterlockedDecrement(mem)

#elif defined(__LINUX__)
#define CORE_ATOMIC_COMPARE_AND_SWAP_32(mem, newVal, oldVal) \
    !(atomic_compare_and_exchange_bool_rel(mem, newVal, oldVal))

    
#define CORE_ATOMIC_COMPARE_AND_SWAP_PTR(mem, newVal, oldVal) \
    !(atomic_compare_and_exchange_bool_rel(mem, (CoreINT_U32) newVal, (CoreINT_U32) oldVal))
    
#define CORE_ATOMIC_INCREMENT(mem) atomic_increment(mem);
#define CORE_ATOMIC_DECREMENT(mem) atomic_decrement(mem);


#endif /* defined(__WIN32__) */




/*****************************************************************************
 *
 * Bits macros and functions...
 * 
 ****************************************************************************/   

CORE_INLINE CoreINT_S32 
CoreBits_mostSignificantBit(CoreINT_U32 n)
{
    CoreINT_S32 b = 0;
    if (0 != (n & (~0u << (1 << 4)))) { b |= (1 << 4); n >>= (1 << 4); }
    if (0 != (n & (~0u << (1 << 3)))) { b |= (1 << 3); n >>= (1 << 3); }
    if (0 != (n & (~0u << (1 << 2)))) { b |= (1 << 2); n >>= (1 << 2); }
    if (0 != (n & (~0u << (1 << 1)))) { b |= (1 << 1); n >>= (1 << 1); }
    if (0 != (n & (~0u << (1 << 0)))) { b |= (1 << 0); }
    return b;
}

CORE_INLINE CoreINT_S32 
CoreBits_leastSignificantBit(CoreINT_U32 n)
{
    CoreINT_S32 b = 31;
    if (n == 0) return -1;
    if ((n & 0x0000ffffL) != 0) { b -= (1 << 4); } else { n >>= (1 << 4); } 
    if ((n & 0x000000ffL) != 0) { b -= (1 << 3); } else { n >>= (1 << 3); }
    if ((n & 0x0000000fL) != 0) { b -= (1 << 2); } else { n >>= (1 << 2); }
    if ((n & 0x00000003L) != 0) { b -= (1 << 1); } else { n >>= (1 << 1); }
    if ((n & 0x00000001L) != 0) { b -= (1 << 0); }				
    return b;
}


/*
 * Masks the bits from starting bit S with length L. 
 * For example:
 *  - mask(0, 1) will mask the first bit
 *  - mask(31, 1) will mask the last bit
 *  - mask(0, 32) will mask the whole u32
 * Basically, it is an AND instruction with a value corresponding to 
 * range <S, S+L> of bits.
 *    
 * Procedure on example mask(2, 4):
 *  1) set all bits:            V = 1111 1111 1111 1111 1111 1111 1111 1111
 *                                                                  ^^ ^^ 
 *  2) shift to left by 28:     V = 1111 0000 0000 0000 0000 0000 0000 0000
 *  3) shift to right by 26:    V = 0000 0000 0000 0000 0000 0000 0011 1100 
 */      
#define CoreBitfield_mask(S, L) \
    ((((CoreINT_U32) ~0UL) << (32UL - (L))) >> (32UL - ((S) + (L))))

/*
 * Gets the bits in <S, S+L> range from the V bitfield.
 * Results in two bit-instructions: AND followed by SHR. 
 */ 
#define CoreBitfield_getValue(V, S, L)      \
    (((V) & CoreBitfield_mask(S, L)) >> (S))

/*
 * Sets the bits in <S, S+L> range in the V bitfield to X.
 * Results in two bit-instructions: AND followed by OR.
 */  
#define CoreBitfield_setValue(V, S, L, X) \
    ((V) = ((V) & ~CoreBitfield_mask(S, L)) | \
            (((X) << (S)) & CoreBitfield_mask(S, L)))

/*
 * Checks whether the N-th bit in the V bitfield is set.
 */
#define CoreBitfield_isSet(V, N) (((V) & (1UL << (N))) != 0)

/*
 * Sets the N-th bit in the V bitfield to 1.
 */
#define CoreBitfield_set(V, N) ((V) |= (1UL << (N)))

/*
 * Sets the N-th bit in the V bitfield to 0.
 */
#define CoreBitfield_clear(V, N) ((V) &= ~(1UL << (N)))

/*
 * Exchange the value of the N-th bit in the V bitfield.
 */
#define CoreBitfield_flip(V, N) ((V) ^= (1UL << (N)))




/*****************************************************************************
 *
 *  ASSERTIONS AND LOGGING MECHANISMS
 *  
 *****************************************************************************/

#if defined(DEBUG)

#if defined(__WIN32__) && !defined(__CYGWIN__)
#define CORE_DUMP_MSG(level, msg, ...) \
    do { \
        CoreINT_U32 _l1 = level; \
        CoreINT_U32 _l2 = Core_getLogLevel(); \
        if ((_l1 & _l2) != 0) { \
            FILE * _f = Core_getLogFile(); \
            fprintf(_f, msg, __VA_ARGS__); \
            fflush(_f); \
        } \
    } while (0)
#elif defined(__LINUX__)
#define CORE_DUMP_MSG(level, msg, args...) \
    do { \
        CoreINT_U32 _l1 = level; \
        CoreINT_U32 _l2 = Core_getLogLevel(); \
        if ((_l1 & _l2) != 0) { \
            FILE * _f = Core_getLogFile(); \
            fprintf(_f, msg, ## args); \
            fflush(_f); \
        } \
    } while (0)
#endif /* defined(__WIN32__) && !defined(__CYGWIN__) */

#else /* (DEBUG) */

#define CORE_DUMP_MSG(level, msg, ...) 

#endif /* (DEBUG) */ 



#define _CORE_DUMP_OBJ_TRACE(_obj, _level, _msg) \
    CORE_DUMP_MSG( \
        (_level), \
        "%s( %s <%p> )\n", \
        _msg, ((CoreClass *)(((CoreRuntimeObject *) _obj)->isa))->name, (void *) _obj \
    )

#if defined(__WIN32__) && !defined(__CYGWIN__)
 
#define CORE_DUMP_OBJ_TRACE(obj, fnc) \
    _CORE_DUMP_OBJ_TRACE(obj, CORE_LOG_TRACE, "->" fnc)

#define CORE_DUMP_TRACE(fnc) \
    CORE_DUMP_MSG(CORE_LOG_TRACE, "->" fnc "\n")


#elif defined (__LINUX__)

#define __STR(x) _VAL(x)
#define _VAL(x) #x
#define CORE_DUMP_OBJ_TRACE(obj, fnc) \
    CORE_DUMP_MSG(CORE_LOG_TRACE, "->"); \
    _CORE_DUMP_OBJ_TRACE(obj, CORE_LOG_TRACE, fnc)

#define CORE_DUMP_TRACE(fnc) \
    CORE_DUMP_MSG(CORE_LOG_TRACE, "->"); \
    CORE_DUMP_MSG(CORE_LOG_TRACE, fnc); \
    CORE_DUMP_MSG(CORE_LOG_TRACE, "\n")
    
#endif


#if defined(__WIN32__) && !defined(__CYGWIN__)
    #define CORE_ASSERT(cond, log_level, desc, ...) \
        do { \
            if (CORE_UNLIKELY(!(cond))) { \
                CORE_DUMP_MSG(log_level, "Error! in: " desc "\n", __VA_ARGS__); \
            } \
        } \
        while (0)

    #define CORE_ASSERT_RET0(cond, log_level, desc, ...) \
        do { \
            if (CORE_UNLIKELY(!(cond))) { \
                CORE_DUMP_MSG(log_level, "Error! in: " desc "\n", __VA_ARGS__); \
                return ; \
            } \
        } \
        while (0)
        
    #define CORE_ASSERT_RET1(ret, cond, log_level, desc, ...) \
        do { \
            if (CORE_UNLIKELY(!(cond))) { \
                CORE_DUMP_MSG(log_level, "Error! in: " desc "\n", __VA_ARGS__); \
                return (ret); \
            } \
        } \
        while (0)
        
#else /* defined(__WIN32__) && !defined(__CYGWIN__) */

    #define CORE_ASSERT(cond, log_level, desc, args...) \
        do { \
            if (CORE_UNLIKELY(!(cond))) { \
                CORE_DUMP_MSG(log_level, "Error! in: " desc "\n", ## args); \
            } \
        } \
        while (0)

    #define CORE_ASSERT_RET0(cond, log_level, desc, args...) \
        do { \
            if (CORE_UNLIKELY(!(cond))) { \
                CORE_DUMP_MSG(log_level, "Error! in: " desc "\n", ## args); \
                return ; \
            } \
        } \
        while (0)

    #define CORE_ASSERT_RET1(ret, cond, log_level, desc, args...) \
        do { \
            if (CORE_UNLIKELY(!(cond))) { \
                CORE_DUMP_MSG(log_level, "Error! in: " desc "\n", ## args); \
                return (ret); \
            } \
        } \
        while (0)
#endif /* defined(__WIN32__) && !defined(__CYGWIN__) */


#if defined(DEBUG)
/* implementation in CoreRuntime.c */
CORE_PROTECTED CoreBOOL
Core_isCoreObject(CoreObjectRef o, CoreClassID id, const char * funcName);
#define CORE_VALIDATE_OBJECT(o, id) Core_isCoreObject(o, id, __PRETTY_FUNCTION__)
#else
#define CORE_VALIDATE_OBJECT(o, id) ((CoreBOOL)true)
#endif




/*****************************************************************************
 *
 *  Threads
 *  
 *****************************************************************************/

#if defined(__WIN32__)
#define Core_getThreadID() ((CoreINT_U32) GetCurrentThreadId())
#elif defined(__LINUX__)
#define Core_getThreadID() ((CoreINT_U32) getpid())
#endif




/*****************************************************************************
 *
 *  Time
 *  
 *****************************************************************************/

#if defined(__LINUX__)
#define CORE_SLEEP_MS(__tm) \
    do \
    { \
        if (__tm == 0) \
        { \
            sched_yield(); \
        } \
        else \
        { \
            struct timespec tm = { 0, (long) (__tm) * 1000000 }; \
            while (nanosleep(&tm, &tm) == -EINTR); \
        } \
    } while (0) \

#elif defined(__WIN32__)
#define CORE_SLEEP_MS(__tm) \
    Sleep((DWORD) (__tm));
#endif

#if defined(__LINUX__)
#define CORE_NSEC_PER_SEC	1000000000L
CORE_INLINE void 
core_set_normalized_timespec(
    struct timespec *ts, 
    time_t sec, 
    CoreINT_S32 nsec
)
{
    while (nsec >= CORE_NSEC_PER_SEC) 
    {
        nsec -= CORE_NSEC_PER_SEC;
        ++sec;
    }
    while (nsec < 0) 
    {
        nsec += CORE_NSEC_PER_SEC;
        --sec;
    }
    ts->tv_sec = sec;
    ts->tv_nsec = nsec;
}

CORE_INLINE CoreINT_S64 
core_timespec_to_ns(const struct timespec *ts)
{
	return ((CoreINT_S64) ts->tv_sec * CORE_NSEC_PER_SEC) + ts->tv_nsec;
}
#endif


#if defined(__WIN32__)

#define EPOCH_DELTA_IN_USEC 11644473600000000ULL /* Microseconds difference between epochs. */
#define EPOCH_DELTA_IN_MSEC EPOCH_DELTA_IN_USEC / 1000 /* Milliseconds difference between epochs. */

CORE_INLINE CoreINT_S64
Core_getCurrentTime_ms(void)
{
    CoreINT_S64 result;
    FILETIME fileTime;

    GetSystemTimeAsFileTime(&fileTime);
    result = (LONGLONG)fileTime.dwLowDateTime + 
        ((LONGLONG)(fileTime.dwHighDateTime) << 32LL);
    result /= 10000;
    result -= EPOCH_DELTA_IN_MSEC;
        
    return result;
}
#elif defined(__LINUX__)
CORE_INLINE CoreINT_S64
Core_getCurrentTime_ms(void)
{
    struct timespec now;
    struct timespec norm_now;
    CoreINT_S64 now_ns;
    
    clock_gettime(CLOCK_MONOTONIC, &now);
    core_set_normalized_timespec(&norm_now, now.tv_sec, now.tv_nsec);
    now_ns = core_timespec_to_ns(&norm_now);

    return (now_ns / 1000000);
}
#endif


/*****************************************************************************
 *
 *  Misc
 *  
 *****************************************************************************/

typedef struct __CoreIteratorState
{
    CoreINT_U32 state;
    const void ** items; 
} __CoreIteratorState;


#define __core_bulk_iterate(__coll_type, __coll, __fnc, __ctx) \
    { \
        CoreINT_U32 __limit; \
        __CoreIteratorState __state; \
        const void * __items[8]; \
        \
        __limit = _ ## __coll_type ## _iterate(__coll, &__state, __items, 8); \
        while (__limit > 0) \
        { \
            CoreINT_U32 __counter; \
            for (__counter = 0; __counter < __limit; __counter++) \
            { \
                __fnc(__state.items[__counter], __ctx); \
            } \
            __limit = _ ## __coll_type ## _iterate(__coll, &__state, __items, 8); \
        } \
    }

#define __core_bulk_iterate_dict(__coll_type, __coll, __fnc, __ctx) \
    { \
        CoreINT_U32 __limit; \
        __CoreIteratorState __state; \
        const void * __items[8]; \
        \
        __limit = _ ## __coll_type ## _iterate(__coll, &__state, __items, 8); \
        while (__limit > 0) \
        { \
            CoreINT_U32 __counter = 0; \
            do \
            { \
                const void * __key = __state.items[__counter++]; \
                const void * __val = __state.items[__counter++]; \
                __fnc(__key, __val, __ctx); \
            } \
            while (__counter < __limit); \
            __limit = _ ## __coll_type ## _iterate(__coll, &__state, __items, 8); \
        } \
    }

#endif // CoreInternal_H
