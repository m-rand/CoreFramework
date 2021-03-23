
#ifndef CoreNotificationCenter_H 

#define CoreNotificationCenter_H 


#include "CoreBase.h"
#include "CoreDictionary.h"


typedef struct __CoreNotificationCenter * CoreNotificationCenterRef;

typedef struct CoreNotificationUserInfo
{
    void * info;
    const void * (* retain) (const void * info);
    void (* release) (const void * info);
} CoreNotificationUserInfo;


typedef void (* CoreNotificationCallback) (
    CoreNotificationCenterRef center,
    CoreImmutableStringRef noteName,
    const void * observer,
    const void * sender,
    void * data,
    void * userInfo
);


CORE_PUBLIC CoreNotificationCenterRef
CoreNotificationCenter_getCenter(void);


CORE_PUBLIC void
CoreNotificationCenter_addObserver(
    CoreNotificationCenterRef center,
    const void * observer,
    CoreNotificationCallback callback,
    CoreImmutableStringRef name,
    const void * sender,
    CoreINT_U32 options,
    CoreNotificationUserInfo * userInfo
);
        
        
CORE_PUBLIC void
CoreNotificationCenter_removeObserver(
    CoreNotificationCenterRef center,
    const void * observer,
    CoreImmutableStringRef name,
    const void * sender
);


CORE_PUBLIC void
CoreNotificationCenter_postNotification(
    CoreNotificationCenterRef center,
    CoreImmutableStringRef name,
    const void * sender,
    const void * data,
    CoreINT_U32 options
);

CORE_PROTECTED CoreNotificationCenterRef
CoreNotificationCenter_create(void);



CORE_PROTECTED void
CoreNotificationCenter_initialize(void);



#endif
