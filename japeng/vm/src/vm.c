#include "vm.h"
#include "common.h"
#include "builtins.h"
#include "dispatch.h"
#include <stdio.h>
#include <string.h>

#define DEBUG_TRACE_EXECUTION 0

#if DEBUG_TRACE_EXECUTION 
static void print_stack(VM* vm);

const char* op_kind[OP_MAX] = {
    [OP_CONSTANT]      = "OP_CONSTANT",
    [OP_GET_LOCAL]     = "OP_GET_LOCAL",
    [OP_SET_LOCAL]     = "OP_SET_LOCAL",
    [OP_GET_GLOBAL]    = "OP_GET_GLOBAL",
    [OP_SET_GLOBAL]    = "OP_SET_GLOBAL",
    [OP_GET_FIELD]     = "OP_GET_FIELD",
    [OP_SET_FIELD]    = "OP_SET_FIELD",
    [OP_SEND]          = "OP_SEND",
    [OP_CALL]          = "OP_CALL",
    [OP_POP]           = "OP_POP",
    [OP_DUP]           = "OP_DUP",
    [OP_RETURN]        = "OP_RETURN",
    [OP_JUMP]          = "OP_JUMP",
    [OP_JUMP_IF_FALSE] = "OP_JUMP_IF_FALSE",
    [OP_LOOP]          = "OP_LOOP",
    [OP_ADD]          = "OP_ADD",
    [OP_SUB]          = "OP_SUB",
    [OP_MUL]          = "OP_MUL",
    [OP_DIV]          = "OP_DIV",
    [OP_LESS]         = "OP_LESS",
    [OP_GREAT]        = "OP_GREAT",
    [OP_EQUAL]        = "OP_EQUAL",
    [OP_CLOSURE]      = "OP_CLOSURE",
    [OP_GET_UPVALUE]  = "OP_GET_UPVALUE",
    [OP_NEW_INSTANCE] = "OP_NEW_INSTANCE"
};
#endif

static void reset_stack(VM* vm)
{
    vm->stack_top = vm->stack;
}

void init_vm(VM* vm)
{
    reset_stack(vm);
    vm->globals = new_table(8);
    init_builtin_classes(vm);
}

void free_vm(VM* vm)
{
    table_free(vm->globals);
    (void)vm;
}

void push(VM* vm, Value value)
{
    *vm->stack_top = value;
    vm->stack_top++;
}

Value pop(VM* vm)
{
    vm->stack_top--;
    return *vm->stack_top;
}

ObjClass* get_class_for_value(VM* vm, Value val) 
{
    if (is_int(val)) {
        return vm->class_integer;
    }

    if (is_obj(val)) {
        Obj* obj = as_obj(val);
        switch (obj->type) {
            case OBJ_INSTANCE:
                return ((ObjInstance*)obj)->klass;
            case OBJ_LIST:
                return vm->class_list;
            default:
                break;
        }
    }
    return vm->class_object;
}

static inline bool is_falsy(Value value) {
    return is_nil(value) || (is_bool(value) && !as_bool(value));
}

static InterpretResult run(VM* vm)
{
    CallFrame* frame = &vm->frames[vm->frame_count - 1]; 
    for(;;) {
#if DEBUG_TRACE_EXECUTION
        print_stack(vm);
        uint32_t current_ip = (uint32_t)(frame->ip - frame->chunk->code);
        uint32_t instruction_preview = *frame->ip;
        Opcode op_preview = get_op(instruction_preview);
        printf("%04d  op: %s (operand: %u)\n", 
               current_ip, op_kind[op_preview], get_operand(instruction_preview));
#endif
       if (frame->ip >= frame->chunk->code + frame->chunk->count) {
            fprintf(stderr, "Runtime Error: Execution fell off the end of chunk without OP_RETURN.\n");
            return INTERPRET_RUNTIME_ERROR;
        }
        uint32_t instruction = *frame->ip++;
        Opcode op = get_op(instruction);

        switch(op) {
            case OP_CONSTANT: {
                uint32_t index = get_operand(instruction);
                Value constant = frame->chunk->constants[index];
                push(vm, constant);
                break;
            }
            case OP_GET_LOCAL: {
                uint32_t slot = get_operand(instruction);
                push(vm, frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint32_t slot = get_operand(instruction);
                frame->slots[slot] = peek(vm ,0);
                break;
            }
            case OP_GET_GLOBAL: {
                uint32_t name_idx = get_operand(instruction);
                ObjString* name = (ObjString*)as_obj(frame->chunk->constants[name_idx]);
                Value value;
                if (!table_get(vm->globals, name, &value)) {
                    fprintf(stderr, "Undefined global variable '%s'\n", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(vm, value);
                break;
            }
            case OP_SET_GLOBAL: {
                uint32_t name_idx = get_operand(instruction);
                ObjString* name = (ObjString*)as_obj(frame->chunk->constants[name_idx]);
                table_set(vm->globals, name, peek(vm, 0));
                break;
            }
            case OP_GET_FIELD: {
                uint32_t field_idx = get_operand(instruction);
                Value receiver = pop(vm);

                ObjInstance* instance = (ObjInstance*)as_obj(receiver);
                push(vm, instance->fields[field_idx]);
                break;
            }
            case OP_SET_FIELD: {
                uint32_t field_idx = get_operand(instruction);
                Value value = pop(vm);
                Value receiver = pop(vm);

                ObjInstance* instance = (ObjInstance*)as_obj(receiver);
                instance->fields[field_idx] = value;
                push(vm, value);
                break;
            }
            case OP_NEW_INSTANCE: {
                Value class_val = pop(vm);
                if (!is_obj(class_val) || ((Obj*)as_obj(class_val))->type != OBJ_CLASS) {
                    fprintf(stderr, "Cannot instantiate non-class value.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjClass* klass = (ObjClass*)as_obj(class_val);
                if (klass != NULL && klass != vm->class_object && klass->superclass == NULL) {
                    klass->superclass = vm->class_object;
                    klass->superclass_type = new_type_info(intern_cstr("Object"), 0);
                }
                if (klass == vm->class_list) {
                    push(vm, make_obj((Obj*)new_list()));
                } else {
                    push(vm, make_obj((Obj*)new_instance(klass)));
                }
                break;
            }
            case OP_CLOSURE: {
                uint32_t fn_idx = get_operand(instruction);
                ObjFunction* fn = (ObjFunction*)as_obj(frame->chunk->constants[fn_idx]);
                ObjClosure* closure = new_closure(fn);

                for (int i = 0; i < fn->upvalue_count; i++) {
                    uint8_t index = fn->upvalues[i].index;
                    if (fn->upvalues[i].is_local) {
                        closure->captures[i] = frame->slots[index];
                    } else {
                        closure->captures[i] = frame->closure->captures[index];
                    }
                }
                push(vm, make_obj((Obj*)closure));
                break;
            }
            case OP_GET_UPVALUE: {
                uint32_t slot = get_operand(instruction);
                push(vm, frame->closure->captures[slot]);
                break;
            }
            case OP_JUMP: {
                uint32_t offset = get_operand(instruction);
                frame->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint32_t offset = get_operand(instruction);
                Value condition = peek(vm, 0);
                if (is_falsy(condition)) frame->ip += offset;
                break;
            }
            case OP_LOOP: {
                uint32_t offset = get_operand(instruction);
                frame->ip -= offset;
                break;
            }
            case OP_SEND: {
                uint8_t arg_count = get_send_args(instruction);
                uint16_t msg_index = get_send_index(instruction);

                if (!dispatch_send(vm, arg_count, msg_index)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm->frames[vm->frame_count - 1];
                break;
            }
            case OP_POP:
                pop(vm);
                break;
            case OP_DUP: 
                push(vm, peek(vm, 0));
                break; 
            case OP_RETURN: {
                Value result = pop(vm);

                vm->frame_count--;

                if (vm->frame_count == 0) {
                    // if (is_int(result)) {
                    //     printf("Result: %d\n", as_int(result));
                    // } else if (is_float(result)) {
                    //     printf("Result: %g\n", as_float(result));
                    // } else if (is_bool(result)) {
                    //     printf("Result: %s\n", as_bool(result) ? "true" : "false");
                    // }
                    return INTERPRET_OK;
                }
                vm->stack_top = frame->slots;

                push(vm ,result);
                frame = &vm->frames[vm->frame_count - 1];
                break;
            }
            case OP_ADD: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (is_int(a) && is_int(b)) {
                    push(vm, make_int(as_int(a) + as_int(b)));
                } else if (is_float(a) && is_float(b)) {
                    push(vm, make_float(as_float(a) + as_float(b)));
                } else {
                    fprintf(stderr, "Type error in OP_ADD.\n");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SUB: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (is_int(a) && is_int(b)) {
                    push(vm, make_int(as_int(a) - as_int(b)));
                } else if (is_float(a) && is_float(b)) {
                    push(vm, make_float(as_float(a) - as_float(b)));
                } else {
                    fprintf(stderr, "Type error in OP_SUB.\n");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_MUL: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (is_int(a) && is_int(b)) {
                    push(vm, make_int(as_int(a) * as_int(b)));
                } else if (is_float(a) && is_float(b)) {
                    push(vm, make_float(as_float(a) * as_float(b)));
                } else {
                    fprintf(stderr, "Type error in OP_MUL.\n");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_DIV: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (is_int(a) && is_int(b)) {
                    if (as_int(b) == 0) {
                        fprintf(stderr, "Division by zero.\n");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    push(vm, make_int(as_int(a) / as_int(b)));
                } else if (is_float(a) && is_float(b)) {
                    push(vm, make_float(as_float(a) / as_float(b)));
                } else {
                    fprintf(stderr, "Type error in OP_DIV.\n");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_LESS: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (is_int(a) && is_int(b)) {
                    push(vm, make_bool(as_int(a) < as_int(b)));
                } else if (is_float(a) && is_float(b)) {
                    push(vm, make_bool(as_float(a) < as_float(b)));
                } else {
                    fprintf(stderr, "Type error in OP_LESS.\n");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_GREAT: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (is_int(a) && is_int(b)) {
                    push(vm, make_bool(as_int(a) > as_int(b)));
                } else if (is_float(a) && is_float(b)) {
                    push(vm, make_bool(as_float(a) > as_float(b)));
                } else {
                    fprintf(stderr, "Type error in OP_GREAT.\n");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_EQUAL: {
                Value b = pop(vm);
                Value a = pop(vm);
                push(vm, make_bool(a == b));
                break;
            }
            default: 
                fprintf(stderr, "Unknown opcode: %d\n", op);
                return INTERPRET_RUNTIME_ERROR;
        }
    }
}

InterpretResult interpret(VM* vm, Chunk* chunk)
{
    reset_stack(vm);
    push(vm, make_nil());
    
    CallFrame* frame = &vm->frames[vm->frame_count++];
    frame->closure = NULL;
    frame->chunk = chunk;
    frame->ip = chunk->code;
    frame->slots = vm->stack;
    return run(vm);
}

#if DEBUG_TRACE_EXECUTION
static void print_stack(VM* vm) {
    printf("\n          ");
    if (vm->stack == vm->stack_top) {
        printf("[ empty ]\n");
        return;
    }
    for (Value* slot = vm->stack; slot < vm->stack_top; slot++) {
        for (int f = 0; f < vm->frame_count; f++) {
            if (slot == vm->frames[f].slots) {
                printf("| f%d: ", f);
            }
        }
        printf("[ ");
        if (is_int(*slot)) {
            printf("%d", as_int(*slot));
        } else if (is_bool(*slot)) {
            printf("%s", as_bool(*slot) ? "true" : "false");
        } else if (is_nil(*slot)) {
            printf("nil");
        } else if (is_float(*slot)) {
            printf("%g", as_float(*slot));
        } else if (is_obj(*slot)) {
            Obj* obj = (Obj*)as_obj(*slot);
            if (obj == NULL) {
                printf("XXX");
            } else {
                switch (obj->type) {
                    case OBJ_CLASS: {
                        ObjClass* klass = (ObjClass*)obj;
                        printf("<class %s>", klass->name ? klass->name->chars : "Anon");
                        break;
                    }
                    case OBJ_INSTANCE: {
                        ObjInstance* inst = (ObjInstance*)obj;
                        printf("<inst %s>", (inst->klass && inst->klass->name) 
                                            ? inst->klass->name->chars : "Anon");
                        break;
                    }
                    case OBJ_STRING: {
                        ObjString* str = (ObjString*)obj;
                        printf("\"%s\"", str->chars);
                        break;
                    }
                    case OBJ_LIST: {
                        ObjList* list = (ObjList*)obj;
                        printf("<list len:%d>", list->size);
                        break;
                    }
                    case OBJ_FUNCTION:
                        printf("<fn>");
                        break;
                    case OBJ_CLOSURE:
                        printf("<closure>");
                        break;
                    default:
                        printf("XXX");
                        break;
                }
            }
        } else {
            printf("<unknown>");
        }
        printf(" ]");
    }
    printf("\n\n");
}
#endif
