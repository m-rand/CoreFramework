

#include "CoreMessagePort.h"
#include "CoreInternal.h"
#include "CoreRuntime.h"
#include "CoreSet.h"
#include "CoreArray.h"
#include "CoreDictionary.h"
#include "CoreRunLoop.h"
#include "CoreRunLoopPriv.h"
#include "CoreString.h"
#include "CoreSynchronisation.h"



#define CORE_MESSAGE_PORT_SERVER    0
#define CORE_MESSAGE_PORT_CLIENT    1
#define CORE_MESSAGE_PORT_VALID_BIT             0
#define CORE_MESSAGE_PORT_CLIENT_SERVER_BIT     1


struct __CoreMessagePort
{
    CoreRuntimeObject core;
    CoreSpinLock lock;
    CoreImmutableStringRef name;
};

typedef struct __CoreMessagePortServer
{
    CoreRuntimeObject core;
    CoreSpinLock lock;
    CoreImmutableStringRef name;
    CoreRunLoopSourceRef source;
    CoreArrayRef requests;
    CoreMessagePort_callback callback;
    CoreMessagePortUserInfo userInfo;
} __CoreMessagePortServer;

typedef struct __CoreMessagePortClient
{
    CoreRuntimeObject core;
    CoreSpinLock lock;
    CoreImmutableStringRef name;
    CoreRunLoopSourceRef replySource;
    CoreMessagePortRef server;
    CoreINT_S32 counter; 
    CoreDictionaryRef replies;
} __CoreMessagePortClient;



/*
 * Storage of  <CoreImmutableStringRef, CoreMessagePortRef>  pairs. 
 */ 
static CoreSpinLock __CoreMessagePortServerRegistryLock = CORE_SPIN_LOCK_INIT;
static CoreDictionaryRef __CoreMessagePortServerRegistry = NULL;




#define CORE_IS_MESSAGE_PORT(port) CORE_VALIDATE_OBJECT(port, CoreMessagePortID)
#define CORE_IS_MESSAGE_PORT_RET0(port) \
    do { if(!CORE_IS_MESSAGE_PORT(port)) return ;} while (0)
#define CORE_IS_MESSAGE_PORT_RET1(port, ret) \
    do { if(!CORE_IS_MESSAGE_PORT(port)) return (ret);} while (0)



CORE_INLINE void
__CoreMessagePort_lock(CoreMessagePortRef mp)
{
    CoreSpinLock_lock(&mp->lock);
}

CORE_INLINE void
__CoreMessagePort_unlock(CoreMessagePortRef mp)
{
    CoreSpinLock_unlock(&mp->lock);
}

CORE_INLINE void
__CoreMessagePort_setValid(CoreMessagePortRef mp, CoreBOOL value)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) mp)->info, 
        CORE_MESSAGE_PORT_VALID_BIT, 
        1, 
        value
    );
}

CORE_INLINE CoreBOOL
__CoreMessagePort_isValid(CoreMessagePortRef mp)
{
    return CoreBitfield_getValue(
        ((const CoreRuntimeObject *) mp)->info,
        CORE_MESSAGE_PORT_VALID_BIT,
        1
    );
}

CORE_INLINE CoreBOOL
__CoreMessagePort_isServer(CoreMessagePortRef mp)
{
    return (CoreBitfield_getValue(
        ((const CoreRuntimeObject *) mp)->info,
        CORE_MESSAGE_PORT_CLIENT_SERVER_BIT,
        1
    ) == CORE_MESSAGE_PORT_SERVER);
}

CORE_INLINE void
__CoreMessagePort_setServer(CoreMessagePortRef mp)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) mp)->info, 
        CORE_MESSAGE_PORT_CLIENT_SERVER_BIT, 
        1, 
        CORE_MESSAGE_PORT_SERVER
    );
}

CORE_INLINE CoreBOOL
__CoreMessagePort_isClient(CoreMessagePortRef mp)
{
    return (CoreBitfield_getValue(
        ((const CoreRuntimeObject *) mp)->info,
        CORE_MESSAGE_PORT_CLIENT_SERVER_BIT,
        1
    ) == CORE_MESSAGE_PORT_CLIENT);
}

CORE_INLINE void
__CoreMessagePort_setClient(CoreMessagePortRef mp)
{
    CoreBitfield_setValue(
        ((CoreRuntimeObject *) mp)->info, 
        CORE_MESSAGE_PORT_CLIENT_SERVER_BIT, 
        1, 
        CORE_MESSAGE_PORT_CLIENT
    );
}


static void
__CoreMessagePort_cleanupServer(struct __CoreMessagePortServer * server)
{
    CoreSpinLock_lock(&__CoreMessagePortServerRegistryLock);
    CoreDictionary_removeValue(__CoreMessagePortServerRegistry, server->name);
    CoreSpinLock_unlock(&__CoreMessagePortServerRegistryLock);
    
    if (server->requests != NULL)
    {
        Core_release(server->requests);
    }
    if (server->source != NULL)
    {
        CoreRunLoopSource_cancel(server->source);
        Core_release(server->source);
    }
    Core_release(server->name);
    CoreSpinLock_cleanup(&server->lock);
}

static void 
__CoreMessagePort_cleanupClient(struct __CoreMessagePortClient * client)
{
    if (client->replies != NULL)
    {
        Core_release(client->replies);
    }
    if (client->server != NULL)
    {
        Core_release(client->server);
    }
    Core_release(client->name);
    CoreSpinLock_cleanup(&client->lock);
}

static void
__CoreMessagePort_cleanup(CoreObjectRef o)
{
    (__CoreMessagePort_isServer((CoreMessagePortRef) o))
        ? __CoreMessagePort_cleanupServer((struct __CoreMessagePortServer *) o)
        : __CoreMessagePort_cleanupClient((struct __CoreMessagePortClient *) o);
}



static CoreClassID CoreMessagePortID = CORE_CLASS_ID_UNKNOWN;

static const CoreClass __CoreMessagePortClass =
{
    0x00,                            // version
    "CoreMessagePort",               // name
    NULL,                            // init
    NULL,                            // copy
    __CoreMessagePort_cleanup,       // cleanup
    NULL,                            // equal
    NULL,                            // hash
    NULL,//__CoreMessagePort_getCopyOfDescription // getCopyOfDescription
};


/* CORE_PROTECTED */ void
CoreMessagePort_initialize(void)
{
    CoreMessagePortID = CoreRuntime_registerClass(&__CoreMessagePortClass);
}




static __CoreMessagePortServer *
__CoreMessagePort_findServer(CoreImmutableStringRef serverName)
{
    __CoreMessagePortServer * result = NULL;

    CoreSpinLock_lock(&__CoreMessagePortServerRegistryLock);
    if (CORE_UNLIKELY(__CoreMessagePortServerRegistry == NULL))
    {
        __CoreMessagePortServerRegistry = CoreDictionary_create(
            CORE_ALLOCATOR_SYSTEM, 0, 
            &CoreDictionaryKeyCoreCallbacks,
            &CoreDictionaryValueCoreCallbacks
        );
    }
    if (CORE_LIKELY(__CoreMessagePortServerRegistry != NULL))
    {
        result = (__CoreMessagePortServer *) CoreDictionary_getValue(
            __CoreMessagePortServerRegistry, serverName
        );
    }
    CoreSpinLock_unlock(&__CoreMessagePortServerRegistryLock);
    
    return result;
}


CoreMessagePortRef
CoreMessagePort_createServer(
    CoreAllocatorRef allocator,
    CoreImmutableStringRef serverName,
    CoreMessagePort_callback callback,
    CoreMessagePortUserInfo * userInfo
)
{
    __CoreMessagePortServer * result = NULL;
    CoreINT_U32 size = sizeof(__CoreMessagePortServer);

    if (serverName == NULL)
    {
        serverName = CORE_EMPTY_STRING;
    }
    
    result = __CoreMessagePort_findServer(serverName);
    if (result != NULL)
    {
        Core_retain(result);
    }
    else
    {
        result = (__CoreMessagePortServer *) CoreRuntime_createObject(
            allocator, CoreMessagePortID, size
        );
    
        if (result != NULL)
        {
            CoreMessagePortRef tmp = NULL;
            CoreMessagePortRef mp = (CoreMessagePortRef) result;
            
            (void) CoreSpinLock_init(&mp->lock);
            result->name = CoreString_createImmutableCopy(
                CORE_ALLOCATOR_SYSTEM, serverName
            );
            result->source = NULL;
            __CoreMessagePort_setServer(mp);
            __CoreMessagePort_setValid(mp, true);
            result->requests = NULL;
            result->callback = callback;
            if (userInfo != NULL)
            {
                memcpy(&result->userInfo, userInfo, sizeof(*userInfo));
                if (userInfo->retain != NULL)
                {
                    userInfo->retain(result->userInfo.info);
                }
            }
            
            // Check again for sure, since someone could add port 
            // with the same name in the meantime.
            CoreSpinLock_lock(&__CoreMessagePortServerRegistryLock);
            tmp = (CoreMessagePortRef) CoreDictionary_getValue(
                __CoreMessagePortServerRegistry, serverName
            );
            if (tmp != NULL)
            {
                Core_retain(tmp);
                Core_release(result);
                result = tmp;
            }
            else
            {
                CoreDictionary_addValue(
                    __CoreMessagePortServerRegistry, serverName, result
                );
            }
            CoreSpinLock_unlock(&__CoreMessagePortServerRegistryLock);            
        }
    }
    
    return (CoreMessagePortRef) result;
}


CORE_PUBLIC CoreMessagePortRef
CoreMessagePort_createClient(
    CoreAllocatorRef allocator,
    CoreImmutableStringRef clientName
)
{
    __CoreMessagePortClient * result = NULL;
    CoreINT_U32 size = sizeof(__CoreMessagePortServer);

    if (clientName == NULL)
    {
        clientName = CORE_EMPTY_STRING;
    }
    
    result = CoreRuntime_createObject(allocator, CoreMessagePortID, size);
    if (result != NULL)
    {
        CoreMessagePortRef mp = (CoreMessagePortRef) result;
        (void) CoreSpinLock_init(&mp->lock);
        result->name = CoreString_createImmutableCopy(
            CORE_ALLOCATOR_SYSTEM, clientName
        );
        result->server = __CoreMessagePort_findServer(result->name);
        result->counter = 0;
        result->replies = CoreDictionary_create(CORE_ALLOCATOR_SYSTEM, 0, NULL, NULL);
        __CoreMessagePort_setClient(mp);
        __CoreMessagePort_setValid(mp, true);
    }
    
    return  (CoreMessagePortRef) result;        
}


static void
__CoreMessagePort_sendReply(
    CoreMessagePortRef mp,
    CoreINT_S32 id,
    CoreINT_S32 msgID,
    CoreDataRef reply
)
{
    if (mp != NULL)
    {
        struct __CoreMessagePortClient * client = 
            (struct __CoreMessagePortClient *) mp;
        CoreRunLoopSourceRef replySource = NULL;
        CoreRunLoopRef replyRunLoop = NULL;
        CoreBOOL wasRegistered = false;
        
        __CoreMessagePort_lock(mp);
        wasRegistered = CoreDictionary_replaceValue(client->replies, id, reply);
        replySource = client->replySource;
        replyRunLoop = CoreRunLoopSource_getRunLoop(replySource);
        __CoreMessagePort_unlock(mp);
        if (wasRegistered && (replySource != NULL))
        {
            Core_retain(reply); // the final release is on mp-client's caller responsibility
            CoreRunLoopSource_signal(replySource);
            CoreRunLoop_wakeUp(replyRunLoop);
        }
    }        
}

// in fact does nothing, signaling this source just wakes up its run-loop
// -- allowing client message port to leave its while-loop in sendRequest() 
static CoreRunLoopSourceRef
__CoreMessagePort_createReplySource(struct __CoreMessagePortClient * client)
{
    CoreRunLoopSourceRef result = NULL;
    CoreRunLoopSourceUserInfo userInfo;
    CoreRunLoopSourceDelegate delegate;
            
    userInfo.info = (void *) client;
    userInfo.retain = Core_retain;
    userInfo.release = Core_release;
    userInfo.getCopyOfDescription = Core_getCopyOfDescription;
    userInfo.equal = NULL;
    userInfo.hash = NULL;
    delegate.schedule = NULL;
    delegate.cancel = NULL;
    delegate.perform = NULL; 
    result = CoreRunLoopSource_create(
        Core_getAllocator(client), &delegate, &userInfo
    );
    
    return result;    
}


struct __CoreMessagePortMessage
{
    CoreINT_S32 id;
    CoreINT_S32 msgID;
    CoreBOOL reply;
    CoreMessagePortRef sender;
    CoreDataRef data;
};

static struct __CoreMessagePortMessage *
__CoreMessagePort_createMessage(
    CoreMessagePortRef sender,
    CoreINT_U32 publicMsgID,
    CoreINT_U32 privateMsgID,
    CoreBOOL response,
    CoreDataRef data
)
{
    struct __CoreMessagePortMessage * result = NULL;

    result = CoreAllocator_allocate(
        CORE_ALLOCATOR_SYSTEM, 
        sizeof(struct __CoreMessagePortMessage)
    );
    if (result != NULL)
    {
        if (data != NULL)
        {
            result->data = CoreData_createImmutableCopy(
                CORE_ALLOCATOR_SYSTEM, data
            );
        }
        result->id = privateMsgID;
        result->msgID = publicMsgID;
        result->reply = response;
        result->sender = sender;
    }
    
    return result;
}


// called on client's thread
static void
__CoreMessagePortServer_addRequest(
    struct __CoreMessagePortServer * server, 
    struct __CoreMessagePortMessage * msg
)
{
    CoreMessagePortRef mp = (CoreMessagePortRef) server;
    CoreINT_U32 count;
    
    __CoreMessagePort_lock(mp);
    if (CORE_UNLIKELY(server->requests == NULL))
    {
        server->requests = CoreArray_create(Core_getAllocator(server), 0, NULL);
    }
    if (CORE_LIKELY(server->requests != NULL))
    {
        count = CoreArray_getCount(server->requests);
        CoreArray_addValue(server->requests, msg);
        __CoreMessagePort_unlock(mp);
        if (count == 0)
        {
            CoreRunLoopSource_signal(server->source);
            CoreRunLoop_wakeUp(CoreRunLoopSource_getRunLoop(server->source));
        }
    }
    else
    {
        __CoreMessagePort_unlock(mp);
    }
}


/* CORE_PUBLIC */ CoreMessagePortResult
CoreMessagePort_sendRequest(
    CoreMessagePortRef mp,
    CoreINT_S32 msgID,
    CoreDataRef data,
    CoreINT_S64 sendTimeout, // currently not available
    CoreINT_S64 recvTimeout,
    CoreImmutableStringRef replyMode,
    CoreDataRef * returnData
)
{
    CoreMessagePortResult result = CORE_MESSAGE_PORT_TRANSPORT_ERROR;
    CoreINT_S32 replyID;
    CoreDataRef reply = NULL;
    CoreBOOL scheduledReplySource = false;
    CoreBOOL response = false;
    CoreRunLoopRef rl = NULL;
    struct __CoreMessagePortMessage * msg;
    struct __CoreMessagePortClient * client = 
        (struct __CoreMessagePortClient *) mp;
        
    CORE_IS_MESSAGE_PORT_RET1(client, CORE_MESSAGE_PORT_INVALID);
    CORE_DUMP_OBJ_TRACE(client, __FUNCTION__);
    CORE_ASSERT_RET1(
        CORE_MESSAGE_PORT_INVALID,
        __CoreMessagePort_isClient(mp),
        CORE_LOG_ASSERT,
        "%s(): function called on non-client port!",
        __PRETTY_FUNCTION__
    );
    
    response = ((replyMode != NULL) && (recvTimeout >= 0));
    
    // Try to get the server when not associated yet.
    __CoreMessagePort_lock(mp);
    if (CORE_UNLIKELY(client->server == NULL))
    {
        client->server = __CoreMessagePort_findServer(mp->name);
    }
    if (CORE_LIKELY(client->server != NULL))
    {
        // Create msg
        replyID = -(client->counter);
        msg = __CoreMessagePort_createMessage(
            mp, msgID, replyID, response, data
        );
        if (msg != NULL)
        {
            result = CORE_MESSAGE_PORT_SUCCESS;
            client->counter++;
            if (response)
            {
                // Well, we will be waiting for the response, so add the ID of
                // this reply to client's replies dictionary. 
                // We will wait on client's runloop until our replySource is
                // signaled.
                CoreDictionary_addValue(client->replies, replyID, NULL);
                if (CORE_UNLIKELY(client->replySource == NULL))
                {
                    client->replySource = __CoreMessagePort_createReplySource(
                        client    
                    );
                }
                if (CORE_LIKELY(client->replySource != NULL))
                {
                    // Now, we should check whether the replySource isn't already
                    // scheduled in this runloop and waiting for another --
                    // previous -- response(s). In this case, we will not schedule 
                    // it again, just reuse it.

                    Core_retain(mp); // so as it doesn't free when waiting on run loop
                    rl = CoreRunLoop_getCurrent();
                    scheduledReplySource = !CoreRunLoop_containsSource(
                        rl, client->replySource, replyMode 
                    );
                    if (scheduledReplySource)
                    {
                        CoreRunLoop_addSource(rl, client->replySource, replyMode);
                    }
                }
                else
                {
                    response = false;
                    result = CORE_MESSAGE_PORT_TRANSPORT_ERROR;
                }
            }
        }
    }
    __CoreMessagePort_unlock(mp);
    
    if (result == CORE_MESSAGE_PORT_SUCCESS)
    {
        // Add request msg into the server's pending requests
        __CoreMessagePortServer_addRequest(client->server, msg);
        
        if (response)
        {
            CoreINT_S64 now = Core_getCurrentTime_ms();
            CoreINT_S64 timeout = now + recvTimeout; // timeout is >= 0 !!
            
            while (now < timeout)
            {
                CoreRunLoop_runInMode(replyMode, timeout, true);
                __CoreMessagePort_lock(mp);
                reply = CoreDictionary_getValue(
                    client->replies, replyID
                );
                __CoreMessagePort_unlock(mp);
                if (reply != NULL)
                {
                    break; // so as we don't call system's time again
                }
                now = Core_getCurrentTime_ms();
            }
            
            // If we did schedule the replySource on runloop, now we remove it.
            if (scheduledReplySource)
            {
                CoreRunLoop_removeSource(rl, client->replySource, replyMode);
            }

            // Remove the reply from replies dictionary (even if we don't have
            // the value!).
            CoreDictionary_removeValue(client->replies, replyID);
            if (reply != NULL)
            {
                *returnData = reply;
            }
            else if (now > timeout)
            {
                result = CORE_MESSAGE_PORT_RECEIVE_TIMEOUT;
            }
            else 
            { 
                // port became invalid -- 
                // TODO: check must be added into the while-loop
            }
            
            Core_release(mp);
        }
    }
    
    return result;
}


/*
 * Process max. 32 of requests at a time. If there are more pending requests, 
 * generate another source's signal and thus postpone the processing for next
 * runloop's loop.
 */    
static void
__CoreMessagePort_processRequest(void * info)
{
    __CoreMessagePortServer * server = (__CoreMessagePortServer *) info;
    CoreMessagePortRef mp = (CoreMessagePortRef) server;
    CoreObjectRef * requests = NULL;
    CoreINT_U32 count, cnt;
        
    __CoreMessagePort_lock(mp);
    if (server->requests != NULL)
    {
        CoreObjectRef buffer[32];
        
        count = CoreArray_getCount(server->requests);
        requests = buffer;
        if (count > 0)
        {
            cnt = min(count, 32);
            CoreArray_copyValues(
                server->requests, CoreRange_make(0, cnt), requests
            );
            CoreArray_replaceValues(
                server->requests,
                CoreRange_make(0, cnt),
                NULL,
                0
            );
        }
    }
    __CoreMessagePort_unlock(mp);
    
    if ((requests != NULL) && (server->callback != NULL))
    {
        CoreINT_U32 idx;
        
        for (idx = 0; idx < cnt; idx++)
        {
            struct __CoreMessagePortMessage * msg = requests[idx];
            CoreDataRef reply = NULL;
            
            reply = server->callback(
                server,
                msg->msgID,
                msg->data,
                msg->sender,
                server->userInfo.info
            );
            if (msg->reply)
            {
                __CoreMessagePort_sendReply(
                    msg->sender, msg->id, msg->msgID, reply
                );
            }
            
            Core_release(msg->data);
            CoreAllocator_deallocate(CORE_ALLOCATOR_SYSTEM, msg);
        } 
    }

    if (count > 32)
    {
        CoreRunLoopSource_signal(server->source);
        CoreRunLoop_wakeUp(CoreRunLoopSource_getRunLoop(server->source));
    }
}


void
CoreMessagePort_scheduleInRunLoop(
    CoreMessagePortRef server,
    CoreRunLoopRef runLoop,
    CoreImmutableStringRef mode
)
{
    struct __CoreMessagePortServer * _server = 
        (struct __CoreMessagePortServer *) server;
        
    CORE_IS_MESSAGE_PORT_RET0(server);
    CORE_DUMP_OBJ_TRACE(server, __FUNCTION__);
    CORE_ASSERT_RET0(
        __CoreMessagePort_isServer(server),
        CORE_LOG_ASSERT,
        "%s(): function called on non-server port!",
        __PRETTY_FUNCTION__
    );
    
    __CoreMessagePort_lock(server);
    if (__CoreMessagePort_isValid(server))
    {
        if (_server->source == NULL)
        {
            CoreRunLoopSourceUserInfo userInfo;
            CoreRunLoopSourceDelegate delegate;
            
            userInfo.info = (void *) server;
            userInfo.retain = Core_retain;
            userInfo.release = Core_release;
            userInfo.getCopyOfDescription = Core_getCopyOfDescription;
            userInfo.equal = NULL;
            userInfo.hash = NULL;
            delegate.schedule = NULL;
            delegate.cancel = NULL;
            delegate.perform = __CoreMessagePort_processRequest;
            _server->source = CoreRunLoopSource_createWithPriority(
                Core_getAllocator(server), &delegate, &userInfo, -100
            );
        }
    }
    __CoreMessagePort_unlock(server);
    
    if (_server->source != NULL)
    {
        CoreRunLoop_addSource(runLoop, _server->source, mode);
        CoreRunLoop_wakeUp(runLoop);
    }
}

