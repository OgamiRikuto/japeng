#include "dispatch/prim_int.h"
#include "common.h"
#include "vm.h"

#include <stdio.h>
#include <string.h>

bool dispatch_integer(VM* vm, const char* method, uint8_t arg_count)
{
    if (strcmp(method, "+") == 0) {
        if (arg_count != 1) {
            fprintf(stderr, "Runtime Error: '+' expects 1 argument, got %d\n", arg_count);
            return false;
        }

        Value b_val = pop(vm);
        Value a_val = pop(vm);

        if (!is_int(b_val)) {
            fprintf(stderr, "Runtime Error: Right operand '+' must be an Integer\n");
            return false;
        }

        int result = as_int(a_val) + as_int(b_val);
        push(vm, make_int(result));
        return true;
    }

    if (strcmp(method, "-") == 0) {
        if (arg_count != 1) {
            fprintf(stderr, "Runtime Error: '-' expects 1 argument, got %d\n", arg_count);
            return false;
        }

        Value b_val = pop(vm);
        Value a_val = pop(vm);

        if (!is_int(b_val)) {
            fprintf(stderr, "Runtime Error: Right operand '-' must be an Integer\n");
            return false;
        }

        int result = as_int(a_val) - as_int(b_val);
        push(vm, make_int(result));
        return true;
    }

    if (strcmp(method, ">") == 0) {
        if (arg_count != 1) {
            fprintf(stderr, "Runtime Error: '>' expects 1 argument, got %d\n", arg_count);
            return false;
        }

        Value b_val = pop(vm);
        Value a_val = pop(vm);

        if (!is_int(b_val) || !is_int(a_val)) {
            fprintf(stderr, "Runtime Error: operand '>' must be an Integer\n");
            return false;
        }

        bool result = as_int(a_val) > as_int(b_val);
        push(vm, make_bool(result));
        return true;

    }
    fprintf(stderr, "Runtime Error: Undefined method '%s' for Integer\n", method);
    return false;
}
