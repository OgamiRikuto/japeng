#include "dispatch.h"
#include "common.h"
#include "vm.h"
#include "builtins.h"
#include "native.h"

#include <stdio.h>
#include <string.h>
ObjString* sym_new = NULL;

bool dispatch_send(VM* vm, uint8_t arg_count, uint16_t msg_index)
{
    CallFrame* frame = &vm->frames[vm->frame_count - 1];
    Value msg_val = frame->chunk->constants[msg_index];
    ObjString* msg_sym = (ObjString*)as_obj(msg_val);

    Value receiver = peek(vm, arg_count);

    // 1. クラスオブジェクトへの 'new' メッセージ
    if (is_obj(receiver) && ((Obj*)as_obj(receiver))->type == OBJ_CLASS) {
        ObjClass* klass = (ObjClass*)as_obj(receiver);
        if (msg_sym == sym_new) {
            ObjInstance* inst = new_instance(klass);
            *(vm->stack_top - 1 - arg_count) = make_obj((Obj*)inst);
            return true;
        }
        fprintf(stderr, "Runtime Error: Undefined class method '%s' for '%s'\n",
                msg_sym->chars, klass->name ? klass->name->chars : "Anonymous");
        return false;
    }

    // 2. クロージャ / 関数の 'call' メッセージ
    if (is_obj(receiver) && (((Obj*)as_obj(receiver))->type == OBJ_CLOSURE || ((Obj*)as_obj(receiver))->type == OBJ_FUNCTION)) {
        if (strcmp(msg_sym->chars, "call") == 0) {
            ObjFunction* func = (((Obj*)as_obj(receiver))->type == OBJ_CLOSURE)
                                ? ((ObjClosure*)as_obj(receiver))->function
                                : (ObjFunction*)as_obj(receiver);
            ObjClosure* closure = (((Obj*)as_obj(receiver))->type == OBJ_CLOSURE)
                                  ? (ObjClosure*)as_obj(receiver)
                                  : NULL;

            if (func->arity != arg_count) {
                fprintf(stderr, "Runtime Error: Expected %d arguments but got %d.\n",
                        func->arity, arg_count);
                return false;
            }

            if (vm->frame_count >= FRAME_MAX) {
                fprintf(stderr, "Stack overflow: frame limit exceeded.\n");
                return false;
            }

            CallFrame* next_frame = &vm->frames[vm->frame_count++];
            next_frame->closure = closure;
            next_frame->chunk = func->chunk;
            next_frame->ip = func->chunk->code;
            next_frame->slots = vm->stack_top - (arg_count + 1);

            return true;
        }
    }

    // 3. クラスオブジェクトへのメッセージ
    ObjClass* klass = get_class_for_value(vm, receiver);
    Value method_val;

    if (klass != NULL && find_method(klass, msg_sym, &method_val)) {
        if (is_obj(method_val) && ((Obj*)as_obj(method_val))->type == OBJ_NATIVE) {
            ObjNative* native = (ObjNative*)as_obj(method_val);
            return native->function(vm, arg_count);
        }

        if (is_obj(method_val) && ((Obj*)as_obj(method_val))->type == OBJ_FUNCTION) {
            ObjFunction* func = (ObjFunction*)as_obj(method_val);

            if (vm->frame_count >= FRAME_MAX) {
                fprintf(stderr, "Stack overflow: frame limit exceeded.\n");
                return false;
            }

            CallFrame* next_frame = &vm->frames[vm->frame_count++];
            next_frame->closure = NULL;
            next_frame->chunk = func->chunk;
            next_frame->ip = func->chunk->code;
            next_frame->slots = vm->stack_top - (arg_count + 1);

            return true;
        }
    }

    // 4. フィールド自動ゲッター（引数0のインスタンスのみ）
    if (is_obj(receiver) && ((Obj*)as_obj(receiver))->type == OBJ_INSTANCE && arg_count == 0) {
        ObjInstance* inst = (ObjInstance*)as_obj(receiver);
        int field_idx = find_field_index(inst->klass, msg_sym);
        if (field_idx != -1) {
            *(vm->stack_top - 1) = inst->fields[field_idx];
            return true;
        }
    }

    fprintf(stderr, "Runtime Error: Undefined message '%s'\n", msg_sym->chars);
    return false;
}
