#ifndef NATIVE_H
#define NATIVE_H

#include "object.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct vm VM;

typedef bool (*NativeFn)(VM* vm, uint8_t arg_count);

typedef struct objNative {
    Obj header;
    NativeFn function;
    Symbol* name;
} ObjNative;

ObjNative* new_native(NativeFn function, Symbol* name);

#endif
