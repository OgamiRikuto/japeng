#ifndef LIST_H
#define LIST_H

#include "common.h"

typedef struct vm VM;

typedef struct objList {
    Obj header;
    Value* elements;
    int size;
    int capacity;
} ObjList;

ObjList* new_list();




#endif
