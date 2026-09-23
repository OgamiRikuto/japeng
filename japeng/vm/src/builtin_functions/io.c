#include "builtins.h"
#include "vm.h"
#include "common.h"

#include <stdio.h>
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

bool native_println(VM* vm, uint8_t arg_count) 
{
    Value target;
    if (arg_count == 0) {
        target = peek(vm, 0);
    } else if (arg_count == 1) {\
        target = peek(vm, 0);
    } else {
        fprintf(stderr, "Runtime Error: 'println' expects 0 or 1 argument.\n");
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

bool native_print(VM* vm, uint8_t arg_count) 
{
    Value target = peek(vm, 0);
    print_value(target);

    for (int i = 0; i <= arg_count; i++) {
        pop(vm);
    }
    push(vm, target);
    return true;
}
