#include "builtin_functions/list.h"
#include "vm.h"
#include <stdio.h>
#include <stdlib.h>

ObjList* new_list()
{
    ObjList* list = (ObjList*)malloc(sizeof(ObjList));
    list->header.type = OBJ_LIST;
    list->size = 0;
    list->capacity = 8;
    list->elements = (Value*)malloc(sizeof(Value) * list->capacity);
    return list;
}

static int grow(ObjList* list)
{
    int old_capacity = list->capacity;
    if (old_capacity < list->size + 1) {
        int new_capacity = old_capacity * 2;
        list->elements = (Value*)realloc(list->elements, sizeof(Value) * new_capacity);
        list->capacity = new_capacity;
    }
    return list->capacity;
}

bool native_list_new(VM* vm, uint8_t arg_count)
{
    pop(vm); 

    ObjList* list = new_list();
    push(vm, make_obj((Obj*)list));
    return true;
}

bool native_list_init(VM* vm, uint8_t arg_count)
{
    Value* args_start = vm->stack_top - arg_count;
    ObjList* list = (ObjList*)as_obj(*(args_start - 1));

    for (int i = 0; i < arg_count; i++) {
        grow(list);
        list->elements[list->size++] = args_start[i];
    }

    vm->stack_top = args_start;

    return true;
}

bool native_list_push(VM* vm, uint8_t arg_count)
{
    if (arg_count != 1) return false;

    Value val = pop(vm);
    Value receiver = pop(vm);

    ObjList* list = (ObjList*)as_obj(receiver);
    grow(list);
    list->elements[list->size++] = val;

    push(vm, val);
    return true;
}


bool native_list_get(VM* vm, uint8_t arg_count)
{
    if (arg_count != 1) return false;

    Value idx_val = pop(vm);
    Value receiver = pop(vm);

    ObjList* list = (ObjList*)as_obj(receiver);
    int index = (int)as_int(idx_val);

    if (index < 0 || index >= list->size) {
        fprintf(stderr, "Runtime Error: List index out of bounds (index: %d, size: %d).\n", index, list->size);
        return false;
    }

    push(vm, list->elements[index]);
    return true;
}

bool native_list_set(VM* vm, uint8_t arg_count)
{
    if (arg_count != 2) return false;

    Value val = pop(vm);
    Value idx_val = pop(vm);
    Value receiver = pop(vm);

    ObjList* list = (ObjList*)as_obj(receiver);
    int index = (int)as_int(idx_val);

    if (index < 0 || index >= list->size) {
        fprintf(stderr, "Runtime Error: List index out of bounds.\n");
        return false;
    }

    list->elements[index] = val;
    push(vm, val);
    return true;
}

bool native_list_length(VM* vm, uint8_t arg_count)
{
    Value receiver = pop(vm);
    ObjList* list = (ObjList*)as_obj(receiver);

    push(vm, make_int(list->size));
    return true;
}
