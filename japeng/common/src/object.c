#include "object.h"

#include <stdlib.h>
#include <string.h>

ObjFunction* new_function(Chunk* chunk, uint8_t arity)
{
    ObjFunction* fn = (ObjFunction*)malloc(sizeof(ObjFunction));
    fn->header.type = OBJ_FUNCTION;
    fn->chunk = chunk;
    fn->arity = arity;
    return fn;
}

ObjClosure* new_closure(ObjFunction* function)
{
    ObjClosure* closure = (ObjClosure*)malloc(sizeof(ObjClosure));
    closure->header.type = OBJ_CLOSURE;
    closure->function = function;
    closure->capture_count = 0;
    return closure;
}

ObjString* new_string(const char* chars, int length)
{
    ObjString* string = (ObjString*)malloc(sizeof(ObjString) + length + 1);
    string->header.type = OBJ_STRING;
    string->length = length;
    memcpy(string->chars, chars, length);
    string->hash = 0;
    return string;
}

