#include "vm.h"
#include "builtins.h"
#include "common.h"
#include <stdio.h>

static bool native_string_init(VM* vm, uint8_t arg_count)
{
    Value* args_start = vm->stack_top - arg_count;
    ObjString* string = (ObjString*)as_obj(*(args_start - 1));

    vm->stack_top = args_start;

    return true;
}

static bool native_string_get(VM* vm, uint8_t arg_count)
{
    if (arg_count != 1) return false;

    Value idx_val = pop(vm);
    Value receiver = pop(vm);

    ObjString* string = (ObjString*)as_obj(receiver);
    int index = (int)as_int(idx_val);

    if (index < 0 || index >= string->length) {
        error_runtime(ERR_INDEX_OUT_OF_BOUNDS, index, string->length);
        return false;
    }

    char ch[2] = {string->chars[index], '\0'};
    ObjString* c = new_string(ch, 1);


    push(vm, make_obj((Obj*)c));
    return true;
}

static bool native_string_length(VM* vm, uint8_t arg_count)
{
    Value receiver = pop(vm);
    ObjString* string = (ObjString*)as_obj(receiver);

    push(vm, make_int(string->length));
    return true;
}

// static bool native_string_toInt(VM* vm, uint8_t arg_count)
// {
//     Value receiver = pop(vm);
//     ObjString* string = (ObjString*)as_obj(receiver);
// }

void setup_string(VM* vm, ObjClass* super_class, TypeInfo* t_info)
{
    TypeInfo* s_info = new_type_info(sym_String, 1);
    vm->class_string = new_class(sym_String, s_info, super_class, t_info, 0);

    define_native(vm->class_string, "init", native_string_init);
    define_native(vm->class_string, "get",    native_string_get);
    define_native(vm->class_string, "length", native_string_length);

    table_set(vm->globals, sym_String, make_obj((Obj*)vm->class_string));
}
