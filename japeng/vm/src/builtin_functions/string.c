#include "vm.h"
#include "builtins.h"
#include "common.h"
#include "gc_layout.h"
#include "gc_heap.h"
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

    if (index < 0 || (uint32_t)index >= string->length) {
        error_runtime(ERR_INDEX_OUT_OF_BOUNDS, index, string->length);
        return false;
    }

    const char* s = string_chars(string);
    char ch[2] = { s[index], '\0' };
    ObjString* c = gc_new_string(&vm->heap, ch, 1);

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
    ObjString* delim = NULL;
    Value receiver;

    if (arg_count == 1) {
        Value arg = pop(vm);
        receiver = pop(vm);
        delim = (ObjString*)as_obj(arg);
    } else if (arg_count == 0) {
        receiver = pop(vm);
    } else {
        error_runtime(ERR_WRONG_ARG_COUNT);
        return false;
    }

    ObjString* string = (ObjString*)as_obj(receiver);
    const char* str_chars = string_chars(string);
    ObjList* list = gc_new_list(&vm->heap, 8);

    // 引数なし: 空白・タブ文字で分割
    if (delim == NULL) {
        uint32_t i = 0;
        while (i < string->length) {
            while (i < string->length && (str_chars[i] == ' ' || str_chars[i] == '\t')) {
                i++;
            }
            if (i >= string->length) break;

            uint32_t start = i;
            while (i < string->length && !(str_chars[i] == ' ' || str_chars[i] == '\t')) {
                i++;
            }

            ObjString* part = gc_new_string(&vm->heap, str_chars + start, i - start);
            list_append(vm, list, make_obj((Obj*)part));
        }
        push(vm, make_obj((Obj*)list));
        return true;
    }

    // 引数あり: 指定デリミタで分割
    const char* delim_chars = string_chars(delim);

    if (delim->length == 0) {
        list_append(vm, list, receiver);
        push(vm, make_obj((Obj*)list));
        return true;
    }

    uint32_t start = 0;
    if (string->length >= delim->length) {
        for (uint32_t i = 0; i <= string->length - delim->length; i++) {
            if (memcmp(str_chars + i, delim_chars, delim->length) == 0) {
                uint32_t len = i - start;
                ObjString* part = gc_new_string(&vm->heap, str_chars + start, len);
                list_append(vm, list, make_obj((Obj*)part));
                i += delim->length - 1;
                start = i + 1;
            }
        }
    }

    uint32_t remaining_len = string->length - start;
    ObjString* last_part = gc_new_string(&vm->heap, str_chars + start, remaining_len);
    list_append(vm, list, make_obj((Obj*)last_part));

    push(vm, make_obj((Obj*)list));
    return true;
}

static bool native_string_trim(VM* vm, uint8_t arg_count)
{
    Value receiver = pop(vm);
    ObjString* string = (ObjString*)as_obj(receiver);
    const char* str_chars = string_chars(string);

    uint32_t start = 0;
    while (start < string->length && (str_chars[start] == ' ' || str_chars[start] == '\t')) {
        start++;
    }

    if (start == string->length) {
        ObjString* empty = gc_new_string(&vm->heap, "", 0);
        push(vm, make_obj((Obj*)empty));
        return true;
    }

    int32_t end = (int32_t)string->length - 1;
    while (end >= (int32_t)start && (str_chars[end] == ' ' || str_chars[end] == '\t')) {
        end--;
    }

    uint32_t len = (uint32_t)(end - start + 1);
    ObjString* trimmed = gc_new_string(&vm->heap, str_chars + start, len);
    push(vm, make_obj((Obj*)trimmed));
    return true;
}

void setup_string(VM* vm)
{
    vm->class_string = vm_new_class(sym_String, vm->class_object);

    vm_define_native(vm->class_string, "init",   native_string_init);
    vm_define_native(vm->class_string, "get",    native_string_get);
    vm_define_native(vm->class_string, "length", native_string_length);
    vm_define_native(vm->class_string, "split",  native_string_split);
    vm_define_native(vm->class_string, "trim",   native_string_trim);

    table_set(vm->globals, sym_String, make_obj((Obj*)vm->class_string));
}
