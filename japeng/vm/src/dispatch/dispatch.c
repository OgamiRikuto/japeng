#include "dispatch/dispatch.h"
#include "dispatch/dispatches.h"
#include "common.h"
#include "vm.h"

#include <stdio.h>

bool dispatch_send(VM* vm, uint8_t arg_count, uint16_t msg_index)
{
    CallFrame* frame = &vm->frames[vm->frame_count - 1];
    Value msg_val = frame->chunk->constants[msg_index];
    const char* method_name = (const char*)as_obj(msg_val);

    Value receiver = peek(vm, arg_count);

    if (is_int(receiver)) {
        return dispatch_integer(vm, method_name, arg_count);
    }

    fprintf(stderr, "Runtime Error: Unsupported type for message '%s'\n", method_name);
    return false;
}
