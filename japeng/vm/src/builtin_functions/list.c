#include "vm.h"
#include "builtins.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>

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

static bool native_list_new(VM* vm, uint8_t arg_count)
{
    pop(vm); 

    ObjList* list = new_list();
    push(vm, make_obj((Obj*)list));
    return true;
}

static bool native_list_init(VM* vm, uint8_t arg_count)
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

static bool native_list_push(VM* vm, uint8_t arg_count)
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


static bool native_list_get(VM* vm, uint8_t arg_count)
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

static bool native_list_set(VM* vm, uint8_t arg_count)
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

static bool native_list_length(VM* vm, uint8_t arg_count)
{
    Value receiver = pop(vm);
    ObjList* list = (ObjList*)as_obj(receiver);

    push(vm, make_int(list->size));
    return true;
}

void setup_list(VM* vm, ObjClass* super_class, TypeInfo* t_info)
{
    TypeInfo* l_info = new_type_info(sym_List, 1);
    vm->class_list = new_class(sym_List, l_info, vm->class_object, t_info, 0);

    define_native(vm->class_list, "init",   native_list_init);
    define_native(vm->class_list, "push",   native_list_push);
    define_native(vm->class_list, "get",    native_list_get);
    define_native(vm->class_list, "set",    native_list_set);
    define_native(vm->class_list, "length", native_list_length);

    table_set(vm->globals, sym_List, make_obj((Obj*)vm->class_list));
}
