#include "vm.h"
#include "builtins.h"
#include "common.h"
#include <stdio.h>
#include <string.h>

static bool native_string_init(VM* vm, uint8_t arg_count)
{
    Value* args_start = vm->stack_top - arg_count;

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

static bool native_string_split(VM* vm, uint8_t arg_count)
{
    if (arg_count > 1) {
        error_runtime(ERR_WRONG_ARG_COUNT);
        return false;
    }

    ObjString* delim = NULL;
    if (arg_count == 1) {
        Value arg = pop(vm);
        if (!is_obj(arg) || ((Obj*)as_obj(arg))->type != OBJ_STRING) {
            error_runtime(ERR_TYPE_OP_ADD);
            return false;
        }
        delim = (ObjString*)as_obj(arg);
    }

    Value receiver = pop(vm);
    ObjString* string = (ObjString*)as_obj(receiver);

    ObjList* list = new_list();

    // 引数なし (連続する空白をまとめて区切る)
    if (delim == NULL) {
        int i = 0;
        while (i < string->length) {
            while(i < string->length && (string->chars[i] == ' ' || string->chars[i] == '\t'))
                i++;
            if (i >= string->length) break;

            int start = i;
            while (i < string->length && !(string->chars[i] == ' ' || string->chars[i] == '\t'))
                i++;
            
            ObjString* part = new_string(string->chars + start, i - start);
            list_append(list, make_obj((Obj*)part));
        }
        push(vm, make_obj((Obj*)list));
        return true;
    }
    
    // 引数あり
    if (delim->length == 0) {
        list_append(list, receiver);
        push(vm, make_obj((Obj*)list));
        return true;
    }

    int start = 0;
    for (int i = 0; i <= string->length - delim->length; i++) {
        if (memcmp(string->chars + i, delim->chars, delim->length) == 0) {
            int len = i - start;
            ObjString* part = new_string(string->chars + start, len);
            list_append(list, make_obj((Obj*)part));
            i += delim->length - 1;
            start = i + 1;
        }
    }

    int remaining_len = string->length - start;
    ObjString* last_part = new_string(string->chars + start, remaining_len);
    list_append(list, make_obj((Obj*)last_part));

    push(vm, make_obj((Obj*)list));
    return true;
}

static bool native_string_trim(VM* vm, uint8_t arg_count)
{
    if (arg_count != 0) {
        error_runtime(ERR_WRONG_ARG_COUNT);
        return false;
    }

    Value receiver = pop(vm);
    ObjString* string = (ObjString*)as_obj(receiver);

    int start = 0;
    while (start < string->length && (string->chars[start] == ' ' || string->chars[start] == '\t'))
        start++;
    
    if (start == string->length) {
        push(vm, make_obj((Obj*)new_string("", 0)));
        return true;
    }

    int end = string->length - 1;
    while (end >= start && (string->chars[end] == ' ' || string->chars[end] == '\t'))
        end--;

    if (start == 0 && end == string->length - 1) {
        push(vm, receiver);
        return true;
    }
    
    int len = end - start + 1;
    ObjString* trimmed = new_string(string->chars + start, len);
    push(vm, make_obj((Obj*)trimmed));
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

    define_native(vm->class_string, "init",   native_string_init);
    define_native(vm->class_string, "get",    native_string_get);
    define_native(vm->class_string, "length", native_string_length);
    define_native(vm->class_string, "split",  native_string_split);
    define_native(vm->class_string, "trim",   native_string_trim);

    table_set(vm->globals, sym_String, make_obj((Obj*)vm->class_string));
}
