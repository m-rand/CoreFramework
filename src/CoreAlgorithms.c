

#include "CoreBase.h"
#include "CoreAlgorithms.h"
#include "CoreInternal.h"


CoreINT_S32 Core_binarySearch(
    const void * key,
    const void * base,
    size_t num,
    size_t width,
    CoreComparison (* compare)(const void *, const void *)
)
{
    CoreINT_S32	result;
    CoreCHAR_8 *lo = (CoreCHAR_8 *) base;
    CoreCHAR_8 *hi = (num == 0) ? (CoreCHAR_8 *) base 
                                : (CoreCHAR_8 *) base + ((num - 1) * width);
    CoreINT_U32 half = num / 2;
    
    while (lo <= hi)
    {
        CoreCHAR_8 * mid = lo + (num * width);
        CoreComparison cmp = (* compare)(key, mid);
        
        if (cmp < 0)
        {
            lo = mid + width;
            num = num & 1 ? half : half-1;
        }
        else if (cmp > 0)
        {
            hi = mid - width;
            num = half;
        }
        else
        {
            result = (mid - (CoreCHAR_8 *) base) + num;
            break;
        }
        half = num / 2;
    }
    
    return (result < 0) ? -((CoreINT_S32)((lo - (CoreCHAR_8 *) base) / width) + 1) : result;
}

CoreINT_S32 Core_binarySearch_obj(
    const void * key,
    const void * x[],
    CoreINT_U32 num,
    CoreComparison (* compare)(const void *, const void *)
)
{
    CoreINT_S32	result;
    CoreINT_U32 lo = 0;
    CoreINT_U32 hi = max(0, num);
    
    while (lo <= hi)
    {
        CoreINT_U32 mid = ((lo + hi) >> 1);
        const void * tmp = x[mid];
        CoreComparison cmp = compare(tmp, key);
        
        if (cmp == CORE_COMPARISON_LESS_THAN)
        {
            lo = mid + 1;
        }
        else if (cmp == CORE_COMPARISON_GREATER_THAN)
        {
            hi = mid - 1;
        }
        else
        {
            result = mid;
            break;
        }
    }
           
    return result;
}


CORE_INLINE void swap(CoreCHAR_8 * a, CoreCHAR_8 * b, size_t width)
{
    CoreCHAR_8 tmp;

    while (width--) 
    {
        tmp = *a;
        *a++ = *b;
        *b++ = tmp;
    }
}

/*CORE_INLINE void vecswap(CoreCHAR_8 * a, CoreCHAR_8 * b, CoreINT_S32 n) 	
{
    while (n-- > 0)
    {
        CoreCHAR_8 tmp = *a;
        *a++ = *b;
        *b++ = tmp;
    }
}*/


/*
 * Vector-swap using a 64 bytes struct. Results in 'rep movsd' instructions
 * which should be faster especially on larger vecswaps.
 */  
#define BLOCK_SIZE (64U)

#define SWAP_TYPE(TYPE, x, y) \
{ \
    TYPE _tmp_ = (x); \
    (x) = (y); \
    (y) = _tmp_; \
}

struct __CopyArrayStruct64 
{
    CoreCHAR_8 element[BLOCK_SIZE];
};

struct __CopyArrayStruct32 
{
    CoreCHAR_8 element[BLOCK_SIZE / 2];
};

CORE_INLINE void vecswap(void * a, void * b, CoreINT_U32 size) 
{
    CoreINT_U32 idx;
    CoreINT_U32 n = size / BLOCK_SIZE; // 'shr' used (BLOCK_SIZE is 2^x)

    for (idx = 0; idx < n; idx++) 
    {
        struct __CopyArrayStruct64 * a1 = (struct __CopyArrayStruct64 *) a;
        struct __CopyArrayStruct64 * b1 = (struct __CopyArrayStruct64 *) b;
        SWAP_TYPE(struct __CopyArrayStruct64, a1[idx], b1[idx]);
    }
    a = &(((struct __CopyArrayStruct64 *) a)[idx]);
    b = &(((struct __CopyArrayStruct64 *) b)[idx]);
    size %= BLOCK_SIZE;

    if (size >= BLOCK_SIZE / 2)
    {
        struct __CopyArrayStruct32 * a1 = (struct __CopyArrayStruct32 *) a;
        struct __CopyArrayStruct32 * b1 = (struct __CopyArrayStruct32 *) b;
        SWAP_TYPE(struct __CopyArrayStruct32, a1[0], b1[0]);
        a = &(((struct __CopyArrayStruct32 *) a)[1]);
        b = &(((struct __CopyArrayStruct32 *) b)[1]);
        size %= (BLOCK_SIZE / 2);
    }
        
    for (idx = 0; idx < size; idx++) 
    {
        CoreCHAR_8 * a2 = (CoreCHAR_8 *) a;
        CoreCHAR_8 * b2 = (CoreCHAR_8 *) b;
        SWAP_TYPE(CoreCHAR_8, a2[idx], b2[idx]);
    }
}

#undef BLOCK_SIZE

CORE_INLINE CoreCHAR_8 * med3(
    CoreCHAR_8 * a, CoreCHAR_8 * b, CoreCHAR_8 * c, 
    CoreComparison (* cmp) (const void *, const void *))
{
    return (cmp(a, b) < 0) 
            ? (cmp(b, c) < 0 ? b : (cmp(a, c) < 0 ? c : a ))
            : (cmp(b, c) > 0 ? b : (cmp(a, c) < 0 ? a : c ));
}

CORE_INLINE void shortsort(
    CoreCHAR_8 * lo, CoreCHAR_8 * hi, size_t width, 
    CoreComparison ( *cmp)(const void *, const void *))
{
    while (hi > lo) 
    {
        CoreCHAR_8 * p;
        CoreCHAR_8 * max = lo;
        
        for (p = lo + width; p <= hi; p += width) 
        {
            if (cmp(p, max) > 0) 
            {
                max = p;
            }
        }

        swap(max, hi, width);
        hi -= width;
    }
}

#define SHORTSORT 7

void _Core_quickSort(
    void * base, size_t num, size_t width, 
    CoreComparison (* cmp) (const void *, const void *))
{
    CoreCHAR_8 * x = (CoreCHAR_8 *) base;
    CoreCHAR_8 *a, *b, *c, *d, *l, *m, *n;
    CoreINT_S32 r;


loop:
    if (num < SHORTSORT) 
    {
        shortsort(x, x + num * width, width, cmp);
        return;
    }
    

    // For small arrays, take the middle element.
    m = x + (num / 2) * width;
    
    if (num > SHORTSORT) 
    {
        l = x;
        n = x + (num - 1) * width;
        if (num > 40) 
        {
            // For big arrays, compute pseudomedian of 9.
            CoreINT_U32 d = (num / 8) * width;
            l = med3(l, l + d, l + 2 * d, cmp);
            m = med3(m - d, m, m + d, cmp);
            n = med3(n - 2 * d, n - d, n, cmp);
        }
        
        // For mid-size, use med of 3.
        m = med3(l, m, n, cmp);
    }
    swap(x, m, width);
    a = x + width;
    b = x + width;

    d = x + (num - 1) * width;
    c = d;
    while (true) 
    {
        while ((b <= c) && ((r = cmp(b, x)) <= 0))
        {
            if (r == 0)
            {
                swap(a, b, width);
                a += width;
            }
            b += width;
        }
        while ((b <= c) && ((r = cmp(c, x)) >= 0))
        {
            if (r == 0)
            {
                swap(c, d, width);
                d -= width;
            }
            c -= width;
        }
        if (b > c)
        {
            break;
        }
        swap(b, c, width);
        b += width;
        c -= width;
    }

    //
    // Swap partition elements back to middle.
    //
    n = x + (num * width);
    
    r = min(a - x, b - a);			
    vecswap(x, b - r, r);
    
    r = min(d - c, n - d - width);	
    vecswap(b, n - r, r);

    // 
    // Recursively sort non-partition-elements. For higher part, use
    // iteration instead of recursion. 
    //
    if ((r = b - a) > width) 
    {
        _Core_quickSort(x, r / width, width, cmp);
    }
    if ((r = d - c) > width) 
    {
        x = n - r;
        num = r / width;
        goto loop; 
        //quicksort(x, num, width, cmp);
    }
}

#undef SHORTSORT



CORE_INLINE void swap_obj(const void * x[], CoreINT_U32 a, CoreINT_U32 b)
{
    const void * tmp = x[a];
    x[a] = x[b];
    x[b] = tmp;
}

CORE_INLINE void vecswap_obj(
    const void * x[], CoreINT_U32 a, CoreINT_U32 b, CoreINT_S32 n) 	
{
    CoreINT_U32 idx;
    for (idx = 0; idx < n; idx++, a++, b++)
    {
        swap_obj(x, a, b);
    }
}

CORE_INLINE CoreINT_U32 med3_obj(
    const void * x[], CoreINT_U32 a, CoreINT_U32 b, CoreINT_U32 c,
    CoreComparison (* cmp)(const void *, const void *)) 
{
    return (cmp(x[a], x[b]) < 0) 
            ? ((cmp(x[b], x[c]) < 0) ? b : (cmp(x[a], x[c]) < 0) ? c : a) 
            : ((cmp(x[b], x[c]) > 0) ? b : (cmp(x[a], x[c]) > 0) ? c : a);
}

CORE_INLINE void shortsort_obj(
    const void * x[], CoreINT_U32 from, CoreINT_U32 to, 
    CoreComparison (* cmp)(const void *, const void *))
{
    CoreINT_U32 i;
    
    for (i = from; i < to; i++)
    {
        CoreINT_U32 j;
        
        for (j = i; (j > from) && (cmp(x[j-1], x[j]) > 0); j--)
        {
            swap_obj(x, j, j-1);
        }
    }
}

#define SHORTSORT 7

void _Core_quickSort_obj(
    const void * x[], CoreINT_U32 offset, CoreINT_U32 num, 
    CoreComparison (* cmp) (const void *, const void *))
{
    CoreINT_U32 a, b, c, d, m, n, r;
    const void * v;

loop:
    // Insertion sort on smallest arrays
    if (num < SHORTSORT) 
    {
        shortsort_obj(x, offset, offset + num, cmp);
        return;
    }

    // 
    // Choose a partition element -- v
    // 
    
    // For small arrays, take the middle element.
    m = offset + (num >> 1);       
    
    if (num > SHORTSORT) 
    {
        CoreINT_U32 lo = offset;
        CoreINT_U32 hi = offset + num - 1;
        if (num > 40) 
        {        
            // For big arrays, compute pseudomedian of 9.
            CoreINT_U32 s = num / 8;
            lo = med3_obj(x, lo, lo + s, lo + 2 * s, cmp);
            m = med3_obj(x, m - s, m, m + s, cmp);
            n = med3_obj(x, hi - 2 * s, hi - s, hi, cmp);
        }

        // For mid-size, use med of 3.
        m = med3_obj(x, lo, m, hi, cmp); 
    }
    v = x[m];


    //
    // Sort the parts against pivot v.
    //
    a = offset;
    b = a;
    c = offset + num - 1;
    d = c;
    while (true) 
    {
        while ((b <= c) && ((r = cmp(x[b], v)) <= 0))
        {
            if (r == 0)
            {
                swap_obj(x, a, b);
                a++;
            }
            b++;
        }
        while ((b <= c) && ((r = cmp(x[c], v)) >= 0))
        {
            if (r == 0)
            {
                swap_obj(x, c, d);
                d--;
            }
            c--;
        }
        if (b > c)
        {
            break;
        }
        swap_obj(x, b, c);
        b++;
        c--;
    }

    //
    // Swap partition elements back to middle.
    //
    n = offset + num;
    
    r = min(a - offset, b - a); 
    vecswap_obj(x, offset, b - r, r);
    
    r = min(d - c, n - d - 1);  
    vecswap_obj(x, b, n - r, r);

    // 
    // Recursively sort non-partition-elements. For higher part, use
    // iteration instead of recursion. 
    //
    r = b - a;
    if (r > 1)
    {
        _Core_quickSort_obj(x, offset, r, cmp);
    }
    
    r = d - c;
    if (r > 1)
    {
        offset = n - r;
        num = r;
        goto loop;
        //_Core_quicksort_obj(x, n - r, r, cmp);
    }
}


#undef SHORTSORT

