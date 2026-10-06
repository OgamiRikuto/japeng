#include "builtins.h"
#include "vm.h"
#include "common.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

static void print_value(Value value)
{
    if (is_int(value)) {
        printf("%d", as_int(value));
    } else if (is_float(value)) {
        printf("%g", as_float(value));
    } else if (is_bool(value)) {
        printf("%s", as_bool(value) ? "true" : "false");
    } else if (is_nil(value)) {
        printf("nil");
    } else if (is_obj(value)) {
        Obj* obj = as_obj(value);
        if (obj == NULL) {
            printf("nil");
            return;
        }
        switch (obj->type) {
            case OBJ_STRING:
                printf("%s", ((ObjString*)obj)->chars);
                break;
            case OBJ_LIST: {
                ObjList* list = (ObjList*)obj;
                printf("list: [ ");
                for (int index = 0; index < list->size; index++) {
                    print_value(list->elements[index]);
                    printf(", ");
                }
                printf("]");
                break;
            }
            case OBJ_CLASS:
                printf("<class %s>", ((ObjClass*)obj)->name ? ((ObjClass*)obj)->name->chars : "Anon");
                break;
            case OBJ_INSTANCE: {
                ObjInstance* inst = (ObjInstance*)obj;
                printf("<instance %s>", (inst->klass && inst->klass->name) ? inst->klass->name->chars : "Anon");
                break;
            }
            case OBJ_FUNCTION:
                printf("<fn>");
                break;
            case OBJ_CLOSURE:
                printf("<closure>");
                break;
            default:
                printf("<obj>");
                break;
        }
    }
}

static bool native_println(VM* vm, uint8_t arg_count) 
{
    Value target;
    if (arg_count == 0) {
        target = peek(vm, 0);
    } else if (arg_count == 1) {\
        target = peek(vm, 0);
    } else {
        error_runtime(ERR_PRINTLN_ARG_COUNT);
        return false;
    }

    print_value(target);
    printf("\n");

    for (int i = 0; i <= arg_count; i++) {
        pop(vm);
    }

    push(vm, target);
    return true;
}

static bool native_print(VM* vm, uint8_t arg_count) 
{
    Value target = peek(vm, 0);
    print_value(target);

    for (int i = 0; i <= arg_count; i++) {
        pop(vm);
    }
    push(vm, target);
    return true;
}

static bool native_input(VM* vm, uint8_t arg_count)
{
    // 引数が1つ渡されていたら、それをプロンプトとして画面表示
    if (arg_count == 1) {
        Value prompt_val = pop(vm);
        if (is_obj(prompt_val) && ((Obj*)as_obj(prompt_val))->type == OBJ_STRING) {
            ObjString* prompt = (ObjString*)as_obj(prompt_val);
            printf("%s", prompt->chars);
            fflush(stdout); // 即座に出力フラッシュ
        }
    }

    char input[1024];
    if (fgets(input, sizeof(input), stdin) == NULL) {
        push(vm, make_obj((Obj*)new_string("", 0)));
        return true;
    }

    size_t len = strlen(input);
    while (len > 0 && (input[len - 1] == '\n' || input[len - 1] == '\r')) {
        input[--len] = '\0';
    }

    ObjString* string = new_string(input, (int)len);
    push(vm, make_obj((Obj*)string));
    return true;
}

void setup_print(VM* vm, ObjClass* klass)
{
    define_native(klass, "print", native_print);
    define_native(klass, "println", native_println);
    define_native(klass, "input", native_input);
}
