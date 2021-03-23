
#ifndef CoreRuntime_H

#define CoreRuntime_H


#include <CoreFramework/CoreBase.h>





typedef struct CoreClass
{
    CoreINT_U32         version;
    CoreCHAR_8          *name;
    CoreBOOL            (* init)(CoreObjectRef);
    CoreObjectRef       (* copy)(CoreAllocatorRef, CoreObjectRef); 
    void                (* cleanup)(CoreObjectRef); 
    CoreBOOL            (* equal)(CoreObjectRef, CoreObjectRef);
    CoreHashCode        (* hash)(CoreObjectRef); 
    CoreImmutableStringRef  (* getCopyOfDescription)(CoreObjectRef);   
} CoreClass;

    
typedef struct CoreRuntimeObject
{
    void * isa;
    CoreINT_U32 info;
} CoreRuntimeObject;




/* Be aware of endianess!! */
#define CORE_INIT_RUNTIME_CLASS(...) { NULL, 0x0 }


#define CORE_CLASS_ID_UNKNOWN   (CoreClassID) 0



CORE_PROTECTED CoreClassID
CoreRuntime_registerClass(const CoreClass * cls);

CORE_PROTECTED CoreObjectRef
CoreRuntime_createObject(
    CoreAllocatorRef allocator,
    CoreINT_U32 classID,
    CoreINT_U32 size    
);

CORE_PROTECTED void
CoreRuntime_initStaticObject(CoreObjectRef o, CoreClassID classID);

CORE_PROTECTED void
_Core_setObjectClassID(CoreObjectRef o, CoreClassID);

CORE_PROTECTED void
_Core_setRetainCount(CoreObjectRef o, CoreINT_U32 count);

CORE_PROTECTED void * 
_Core_getISAForClassID(CoreClassID);

CORE_PROTECTED CoreAllocatorRef
Core_getAllocator(CoreObjectRef o);



#endif
