#include "object.h"

#include <stdlib.h>

ObjFunction* new_function(Chunk* chunk, uint8_t arity)
{
    ObjFunction* fn = (ObjFunction*)malloc(sizeof(ObjFunction));
    fn->header.type = OBJ_FUNCTION;
    fn->chunk = chunk;
    fn->arity = arity;
    return fn;
}
