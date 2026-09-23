#ifndef LIST_H
#define LIST_H

#include "literal.h"
#include "object.h"

typedef struct vm VM;

typedef struct objList {
    Obj header;
    Value* elements;
    int size;
    int capacity;
} ObjList;

ObjList* new_list();
bool native_list_new(VM* vm, uint8_t arg_count);
bool native_list_init(VM* vm, uint8_t arg_count);
bool native_list_push(VM* vm, uint8_t arg_count);
bool native_list_get(VM* vm, uint8_t arg_count);
bool native_list_set(VM* vm, uint8_t arg_count);
bool native_list_length(VM* vm, uint8_t arg_count);


#endif
