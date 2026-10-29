#include "vm.h"
#include "builtins.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>

static bool native_list_init(VM* vm, uint8_t arg_count)
{
    Value* args_start = vm->stack_top - arg_count;
    ObjList* list = (ObjList*)as_obj(*(args_start - 1));

    for (int i = 0; i < arg_count; i++) {
        gc_list_grow(&vm->heap, list);
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
    gc_list_grow(&vm->heap, list);
    list->elements[list->size++] = val;

    gc_write_barrier(&vm->heap, list, val); // ★ ライトバリア発動
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

    if (index < 0 || (uint32_t)index >= list->size) {
        error_runtime(ERR_INDEX_OUT_OF_BOUNDS, index, list->size);
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

    if (index < 0 || (uint32_t)index >= list->size) {
        error_runtime(ERR_INDEX_OUT_OF_BOUNDS, index, list->size);
        return false;
    }

    list->elements[index] = val;
    gc_write_barrier(&vm->heap, list, val); // ★ ライトバリア発動
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

static bool native_list_swap(VM* vm, uint8_t arg_count)
{
    if (arg_count != 2) {
        error_runtime(ERR_WRONG_ARG_COUNT);
        return false;
    }

    Value arg2 = pop(vm);
    Value arg1 = pop(vm);
    Value receiver = pop(vm);

    if (!is_int(arg1) || !is_int(arg2)) {
        error_runtime(ERR_TYPE_OP_ADD);
        return false;
    }

    int n1 = as_int(arg1);
    int n2 = as_int(arg2);
    ObjList* list = (ObjList*)as_obj(receiver);

    if ((n1 < 0 || (uint32_t)n1 >= list->size) || (n2 < 0 || (uint32_t)n2 >= list->size)) {
        error_runtime(ERR_INDEX_OUT_OF_BOUNDS);
        return false;
    }

    Value tmp = list->elements[n1];
    list->elements[n1] = list->elements[n2];
    list->elements[n2] = tmp;

    push(vm, receiver);
    return true;
}

static bool native_list_map(VM* vm, uint8_t arg_count)
{
    if (arg_count != 1) {
        error_runtime(ERR_WRONG_ARG_COUNT);
        return false;
    }

    Value fn_val = pop(vm);
    Value receiver = pop(vm);

    ObjList* src_list = (ObjList*)as_obj(receiver);
    ObjList* dest_list = gc_new_list(&vm->heap, src_list->size);

    for (uint32_t i = 0; i < src_list->size; i++) {
        Value elem = src_list->elements[i];
        Value mapped_val;

        Value args[1] = { elem };
        if (!vm_call_function(vm, fn_val, 1, args, &mapped_val)) {
            return false;
        }

        gc_list_grow(&vm->heap, dest_list);
        dest_list->elements[dest_list->size++] = mapped_val;
    }

    push(vm, make_obj((Obj*)dest_list));
    return true;
}

static bool native_list_toString(VM* vm, uint8_t arg_count)
{
    Value receiver = pop(vm);
    ObjList* list = (ObjList*)as_obj(receiver);

    // 最大 2048 バイトのバッファで安全に組み立てる
    char buf[2048];
    int p = snprintf(buf, sizeof(buf), "[");

    for (uint32_t i = 0; i < list->size && p < (int)sizeof(buf) - 10; i++) {
        Value val = list->elements[i];
        if (i > 0) p += snprintf(buf + p, sizeof(buf) - p, ", ");

        if (is_int(val)) {
            p += snprintf(buf + p, sizeof(buf) - p, "%d", as_int(val));
        } else if (is_float(val)) {
            p += snprintf(buf + p, sizeof(buf) - p, "%g", as_float(val));
        } else if (is_bool(val)) {
            p += snprintf(buf + p, sizeof(buf) - p, "%s", as_bool(val) ? "true" : "false");
        } else if (is_nil(val)) {
            p += snprintf(buf + p, sizeof(buf) - p, "nil");
        } else if (is_obj(val)) {
            Obj* obj = as_obj(val);
            if (obj->type == OBJ_STRING) {
                p += snprintf(buf + p, sizeof(buf) - p, "\"%s\"", string_chars((ObjString*)obj));
            } else {
                p += snprintf(buf + p, sizeof(buf) - p, "<obj>");
            }
        }
    }
    snprintf(buf + p, sizeof(buf) - p, "]");

    ObjString* str = gc_new_string(&vm->heap, buf, strlen(buf));
    push(vm, make_obj((Obj*)str));
    return true;
}

void setup_list(VM* vm)
{
    vm->class_list = vm_new_class(sym_List, vm->class_object);

    vm_define_native(vm->class_list, "init",   native_list_init);
    vm_define_native(vm->class_list, "push",   native_list_push);
    vm_define_native(vm->class_list, "get",    native_list_get);
    vm_define_native(vm->class_list, "set",    native_list_set);
    vm_define_native(vm->class_list, "length", native_list_length);
    vm_define_native(vm->class_list, "swap",   native_list_swap);
    vm_define_native(vm->class_list, "map",    native_list_map);
    vm_define_native(vm->class_list, "toString", native_list_toString);

    table_set(vm->globals, sym_List, make_obj((Obj*)vm->class_list));
}

void list_append(VM* vm, ObjList* list, Value val)
{
    gc_list_grow(&vm->heap, list);
    list->elements[list->size++] = val;
    gc_write_barrier(&vm->heap, list, val);
}
