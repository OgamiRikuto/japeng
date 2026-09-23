#include "native.h"
#include <stdlib.h>

ObjNative* new_native(NativeFn function, ObjString* name) 
{
    ObjNative* native = (ObjNative*)malloc(sizeof(ObjNative));
    native->header.type = OBJ_NATIVE;
    native->function = function;
    native->name = name;
    return native;
}
