
#ifndef CoreAlgorithms_H 

#define CoreAlgorithms_H 


#include "CoreBase.h"

CoreINT_S32 Core_binarySearch(
	const void * key,
	const void * base,
	size_t num,
	size_t width,
	CoreComparison (* compare)(const void *, const void *)
);

void _Core_quickSort(
    void * base, 
    size_t num, 
    size_t width, 
    CoreComparison (* cmp) (const void *, const void *)
);

void _Core_quickSort_obj(
	const void * x[], 
    CoreINT_U32 offset, 
    CoreINT_U32 num, 
	CoreComparison (* cmp) (const void *, const void *)
);

#endif
