#include "dispatch/dispatch.h"
#include "dispatch/dispatches.h"
#include "common.h"
#include "vm.h"

#include <stdio.h>
#include <string.h>
ObjString* sym_new = NULL;

bool dispatch_send(VM* vm, uint8_t arg_count, uint16_t msg_index)
{
    CallFrame* frame = &vm->frames[vm->frame_count - 1];
    Value msg_val = frame->chunk->constants[msg_index];
    ObjString* msg_sym = (ObjString*)as_obj(msg_val);

    Value receiver = peek(vm, arg_count);

    if (is_int(receiver)) {
        return dispatch_integer(vm, msg_sym->chars, arg_count);
    }

    if (is_obj(receiver)) {
        Obj* obj = as_obj(receiver);

        if (obj->type == OBJ_CLASS) {
            ObjClass* klass = (ObjClass*)obj;

            if (msg_sym == sym_new) {
                ObjInstance* inst = new_instance(klass);
                *(vm->stack_top - 1 - arg_count) = make_obj((Obj*)inst);
                return true;
            }

            fprintf(stderr, "Runtime Error: Undefined class method '%s' for '%s'\n",
                    msg_sym->chars, klass->name ? klass->name->chars : "Anonymous");
            return false;
        }

        if (obj->type == OBJ_INSTANCE) {
            ObjInstance* inst = (ObjInstance*)obj;
            Value method_val;

            if (find_method(inst->klass, msg_sym, &method_val)) {
                ObjFunction* func = (ObjFunction*)as_obj(method_val);

                if (vm->frame_count >= FRAME_MAX) {
                    fprintf(stderr, "Stack overflow: frame limit exceeded.\n");
                    return false;
                }

                CallFrame* next_frame = &vm->frames[vm->frame_count++];
                next_frame->chunk = func->chunk;
                next_frame->ip = func->chunk->code;
                next_frame->slots = vm->stack_top - (arg_count + 1);

                return true;
            }

            fprintf(stderr, "Runtime Error: Undefined method '%s' for instance of '%s'\n",
                    msg_sym->chars, inst->klass->name ? inst->klass->name->chars : "Anonymous");
            return false;
        }
    }

    fprintf(stderr, "Runtime Error: Unsupported type for message '%s'\n", msg_sym->chars);
    return false;
}
