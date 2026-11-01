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

Symbol* new_string(const char* chars, int length)
{
    Symbol* string = (Symbol*)malloc(sizeof(Symbol) + length + 1);
    string->header.type = OBJ_STRING;
    string->length = length;
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    string->hash = 0;
    return string;
}

ObjCallSite* new_call_site(Symbol* interface_name, uint16_t method_index) 
{
    ObjCallSite* cs = (ObjCallSite*)malloc(sizeof(ObjCallSite));
    cs->header.type = OBJ_CALL_SITE;
    cs->interface_name = interface_name;
    cs->method_index = method_index;
    return cs;
}
