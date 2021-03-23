

#include <CoreFramework/CoreNumber.h>
#include "CoreInternal.h"



/*****************************************************************************
 *
 * Types definitions
 * 
 ****************************************************************************/

struct __CoreNumber
{
    CoreRuntimeObject core;
    CoreINT_U64 value;
}

typedef struct CoreINT_S128
{
    CoreINT_S64 high;
    CoreINT_U64 low;
} CoreINT_S128;



static CoreClassID CoreNumberID = CORE_CLASS_ID_NOTHING;

static const CoreClass __CoreNumberClass =
{
    0x00,                               // version
    "CoreNumber",                       // name
    NULL,                               // init
    NULL,                               // copy
    __CoreNumber_cleanup,               // cleanup
    __CoreNumber_equal,                 // equal
    __CoreNumber_hash,                  // hash
    __CoreNumber_getCopyOfDescription   // getCopyOfDescription
};



/*****************************************************************************
 *
 * CoreNumberCache
 * 
 ****************************************************************************/

#define CORE_NUMBER_CACHED_MIN (-3)
#define CORE_NUMBER_CACHED_MAX (10)
static CoreNumber * CoreNumberCacheTable
    [CORE_NUMBER_CACHED_MAX - CORE_NUMBER_CACHED_MIN + 1] = { null };
//static CoreLock * theCoreNumberCacheLock = CoreLock_Create();



/*****************************************************************************
 *
 * CoreNumberInfoTable
 * 
 ****************************************************************************/

typedef struct CoreNumberInfoEntry 
{
    CoreINT_U32 canonicalType;      // storage type
    CoreINT_U32 realSize;           // byte size of real type
    CoreINT_U32 lgStorageSize;      // base-2 log byte size of storage type
    CoreINT_U32 isFloat;            // is float
} CoreNumberInfoEntry;

static const struct CoreNumberInfoEntry CoreNumberInfoTable[] =
{
    { CORE_NUMBER_INT_U32,	4, 3, 0 }, // CORE_NUMBER_BOOL
    { CORE_NUMBER_INT_U8,	1, 3, 0 }, // CORE_NUMBER_CHAR_8
    { CORE_NUMBER_INT_U8,	1, 3, 0 }, // CORE_NUMBER_INT_U8
    { CORE_NUMBER_INT_U16,	2, 3, 0 }, // CORE_NUMBER_INT_U16
    { CORE_NUMBER_INT_U32,	4, 3, 0 }, // CORE_NUMBER_INT_U32
    { CORE_NUMBER_INT_U64,	8, 3, 0 }, // CORE_NUMBER_INT_U64
    { CORE_NUMBER_INT_U8,	1, 3, 0 }, // CORE_NUMBER_INT_S8
    { CORE_NUMBER_INT_U16,	2, 3, 0 }, // CORE_NUMBER_INT_S16
    { CORE_NUMBER_INT_U32,	4, 3, 0 }, // CORE_NUMBER_INT_S32
    { CORE_NUMBER_INT_U64,	8, 3, 0 }, // CORE_NUMBER_INT_S64
    { CORE_NUMBER_REAL_32,	4, 3, 1 }, // CORE_NUMBER_REAL_32
    { CORE_NUMBER_REAL_64,	8, 3, 1 }  // CORE_NUMBER_REAL_64 
};





// Same as memcpy, but for our purposes it is faster, especially when inlined.
CORE_INLINE void
copy(void * dst, const void * src, CoreINT_U32 size)
{
    switch (size)
    {
        case 1:
            *(CoreINT_U8 *)dst = *(CoreINT_U8 *)src;
            break;
        case 2:
            *(CoreINT_U16 *)dst = *(CoreINT_U16 *)src;
            break;
        case 4:
            *(CoreINT_U32 *)dst = *(CoreINT_U32 *)src;
            break;
        case 8:
            *(CoreINT_U64 *)dst = *(CoreINT_U64 *)src;
            break;			
    }			
}


/*
 * Converts a given number into signed int128 value.
 * Note: Now it works correctly only when source 'src' is an integer.
 */    
CORE_INLINE void 
convertToS128(CoreNumberRef src, CoreINT_S128 * dst)
{
    copy(&dst->low, src, sizeof(CoreINT_U64));
    if (src->info.isUnsigned)
    {
        dst->high = 0;
    }
    else
    {
        CoreINT_S64 v;
        copy(&v, src, sizeof(CoreINT_S64));
        dst->high = (v < 0) ? -1LL : 0LL; 
    }
}

CORE_INLINE CoreComparison
compareS128(const CoreINT_S128 * v1, const CoreINT_S128 * v2)
{
    CoreComparison result;

    if (v1->high < v2->high)
    {
        result = CORE_COMPARISON_LESS_THAN;
    }
    else if (v1->high > v2->high)
    {
        result = CORE_COMPARISON_GREATER_THAN;
    }
    else
    {
        if (v1->low < v2->low)
        {
            result = CORE_COMPARISON_LESS_THAN;
        }
        else if (v1->low > v2->low)
        {
            result = CORE_COMPARISON_GREATER_THAN;
        }
        else
        {
            result = CORE_COMPARISON_EQUAL;
        }
    }

    return result;
}


static CoreComparison
__CoreNumber_compare(
    CoreNumberRef me,
    CoreNumberRef to
)
{
    CoreComparison result = CORE_COMPARISON_UNCOMPARABLE;

    if (me == to)
    {
        result = CORE_COMPARISON_EQUAL;
    }
    else
    {
        CoreNumberType meType = me->info.type;
        CoreNumberType toType = to->info.type;
        CoreINT_U32 floats = 0; // first bit:   meType is float
                                // second bit:  toType is float
        
        floats |= (CoreNumberInfoTable[meType].isFloat << 0);
        floats |= (CoreNumberInfoTable[toType].isFloat << 1);

        if (floats == ((1 << 0) | (1 << 1))
        {
            // Both numbers are float.
            CoreREAL_64 v1;
            CoreREAL_64 v2;
            int s1, s2, n1, n2;
            
            // Use the simplest version of get value...
            // we don't care about type conversion, since we use CoreREAL_64
            v1 = CoreNumber_real64Value(me);
            v2 = CoreNumber_real64Value(to);
            s1 = signbit(v1); // non-zero s1 means v1 is negative
            s2 = signbit(v2); // non-zero s2 means v2 is negative
            n1 = isnan(v1); // 1 means v1 is NaN
            n2 = isnan(v2); // 1 means v2 is NaN

            // We should check numbers against NaN
            if ((n1 == 1) && (n2 == 1))
            {
                result = CORE_COMPARISON_EQUAL;
            }
            else if (n1 == 1)
            {
                result = (s2 != 0)  ? CORE_COMPARISON_GREATER_THAN
                                    : CORE_COMPARISON_LESS_THAN;
            }
            else if (n2 == 1)
            {
                result = (s1 != 0)  ? CORE_COMPARISON_LESS_THAN
                                    : CORE_COMPARISON_GREATER_THAN;
            }
            else
            {
                // So we don't have any NaNs, great.
                // Compare signs first.
                if (s1 != 0)
                {
                    if (s2 == 0)
                    {
                        result = CORE_COMPARISON_LESS_THAN;
                    }
                }
                else if (s2 != 0)
                {
                    result = CORE_COMPARISON_GREATER_THAN;
                }

                // If the signs are the same, compare the very values.
                if (result == CORE_COMPARISON_UNCOMPARABLE)
                {
                    if (v1 < v2)
                    {
                        result = CORE_COMPARISON_LESS_THAN;
                    }
                    else if (v2 < v1)
                    {
                        result = CORE_COMPARISON_GREATER_THAN;
                    }
                    else
                    {
                        result = CORE_COMPARISON_EQUAL;
                    }
                }
            }
        }
        else if (floats == 0)
        {
            // Both numbers are integer.
            CoreINT_S128 v1;
            CoreINT_S128 v2;
            
            convertToS128(me, &v1);
            convertToS128(to, &v2);
            
            result = compareS128(&v1, &v2);
        }
        else
        {
            // One number is float, the other is integer.
        }        
    }
    
    return result;
}


CORE_INLINE CoreBOOL
__CoreNumber_equal(CoreNumberRef me, CoreNumberRef to)
{
    return (__CoreNumber_compare(me, to) == CORE_COMPARISON_EQUAL)
        ? true : false;
}


CORE_INLINE CoreBOOL
__CoreNumber_getValue_compatible(
    CoreNumberRef me, 
    CoreNumberType type, 
    void * dst
)
{
    CoreBOOL result = false;

    switch (type)
    {
        case CORE__NUMBER_BOOL:
        case CORE__NUMBER_CHAR_8:
        case CORE__NUMBER_INT_U8:
        case CORE__NUMBER_INT_U16:
        case CORE__NUMBER_INT_U32:
        case CORE__NUMBER_INT_U64:
        case CORE__NUMBER_INT_S8:
        case CORE__NUMBER_INT_S16:
        case CORE__NUMBER_INT_S32:
        case CORE__NUMBER_INT_S64:
        case CORE__NUMBER_REAL_32:
        case CORE__NUMBER_REAL_64:
        {
            result = convert((const void *) &me->value, me->info.type, dst, type);
            break;			
        }
    }
    
    return result;	
} 





















CoreBOOL
CoreNumber_getBoolValue(const CoreNumber * me)
{
    return ((CoreBOOL) me->value);
}

CoreCHAR_8
CoreNumber_getChar8Value(const CoreNumber * me)
{
    return ((CoreCHAR_8) me->value);
}

CoreINT_U8
CoreNumber_getUInt8Value(const CoreNumber * me)
{
    return ((CoreINT_U8) me->value);
}

CoreINT_U16
CoreNumber_getUInt16Value(const CoreNumber * me)
{
    return ((CoreINT_U16) me->value);
}

CoreINT_U32
CoreNumber_getUInt32Value(const CoreNumber * me)
{
    return ((CoreINT_U32) me->value);
}

CoreINT_U64
CoreNumber_getUInt64Value(CoreNumberRef me)
{
    return ((CoreINT_U64) me->value);
}

CoreINT_S8
CoreNumber_getSInt8Value(CoreNumberRef me)
{
    return ((CoreINT_S8) me->value);
}

CoreINT_S16
CoreNumber_getSInt16Value(CoreNumberRef me)
{
    return ((CoreINT_S16) me->value);
}

CoreINT_S32
CoreNumber_getSInt32Value(CoreNumberRef me)
{
    return ((CoreINT_S32) me->value);
}

CoreINT_S64
CoreNumber_getSInt64Value(CoreNumberRef me)
{
    return ((CoreINT_S64) me->value);
}

CoreREAL_64
CoreNumber_getReal64Value(CoreNumberRef me)
{
    return ((CoreREAL_64) me->value);
}

CoreREAL_32
CoreNumber_getReal32Value(CoreNumberRef me)
{
    return ((CoreREAL_32) me->value);
}
