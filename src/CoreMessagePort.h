

#ifndef CoreMessagePort_H 

#define CoreMessagePort_H 


#include <CoreFramework/CoreBase.h>
#include "CoreData.h"
#include "CoreRunLoop.h"



typedef struct __CoreMessagePort * CoreMessagePortRef;


typedef enum CoreMessagePortResult
{
    CORE_MESSAGE_PORT_SUCCESS = 0,
    CORE_MESSAGE_PORT_SEND_TIMEOUT = -1,
    CORE_MESSAGE_PORT_RECEIVE_TIMEOUT = -2,
    CORE_MESSAGE_PORT_TRANSPORT_ERROR = -3,
    CORE_MESSAGE_PORT_INVALID = -4
} CoreMessagePortResult;


/*
 * When non-null result, one is responsible for releasing the CoreData object.
 */  
typedef CoreDataRef (* CoreMessagePort_callback) (
    CoreMessagePortRef server,
    CoreINT_S32 msgID,
    CoreDataRef data,
    CoreMessagePortRef sender,
    void * info
);

typedef struct CoreMessagePortUserInfo 
{
    void * info;
    Core_retainInfoCallback retain;
    Core_releaseInfoCallback release;
    Core_getCopyOfDescriptionCallback getCopyOfDescription;
} CoreMessagePortUserInfo;



CORE_PUBLIC CoreMessagePortRef
CoreMessagePort_createServer(
    CoreAllocatorRef allocator,
    CoreImmutableStringRef serverName,
    CoreMessagePort_callback callback,
    CoreMessagePortUserInfo * userInfo
);

CORE_PUBLIC CoreMessagePortRef
CoreMessagePort_createClient(
    CoreAllocatorRef allocator,
    CoreImmutableStringRef clientName
); 

CORE_PUBLIC CoreMessagePortResult
CoreMessagePort_sendRequest(
    CoreMessagePortRef client,
    CoreINT_S32 msgID,
    CoreDataRef data,
    CoreINT_S64 sendTimeout, // currently not available
    CoreINT_S64 recvTimeout,
    CoreImmutableStringRef replyMode,
    CoreDataRef * returnData/*,
    CoreErrorRef error */
);

CORE_PUBLIC void
CoreMessagePort_scheduleInRunLoop(
    CoreMessagePortRef server,
    CoreRunLoopRef runLoop,
    CoreImmutableStringRef mode
);


/* CORE_PROTECTED */ void
CoreMessagePort_initialize(void);



#endif
