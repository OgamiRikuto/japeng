#include "vm.h"
#include "common.h"
#include "dispatch/dispatches.h"
#include <stdio.h>
#include <string.h>

#define DEBUG_TRACE_EXECUTION 1

#if DEBUG_TRACE_EXECUTION 
static void print_stack(VM* vm);

const char* op_kind[OP_MAX] = {
    [OP_CONSTANT]      = "OP_CONSTANT",
    [OP_GET_LOCAL]     = "OP_GET_LOCAL",
    [OP_SET_LOCAL]     = "OP_SET_LOCAL",
    [OP_SEND]          = "OP_SEND",
    [OP_CALL]          = "OP_CALL",
    [OP_POP]           = "OP_POP",
    [OP_RETURN]        = "OP_RETURN",
    [OP_JUMP]          = "OP_JUMP",
    [OP_JUMP_IF_FALSE] = "OP_JUMP_IF_FALSE",
    [OP_LOOP]          = "OP_LOOP",
};
#endif

static void reset_stack(VM* vm)
{
    vm->stack_top = vm->stack;
}

void init_vm(VM* vm)
{
    reset_stack(vm);
}

void free_vm(VM* vm)
{
    // 未定
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

static bool push_frame(VM* vm, Chunk* chunk, uint8_t arg_count)
{
    if (vm->frame_count >= FRAME_MAX) {
        fprintf(stderr, "Stack overflow: frame limit exceeded.\n");
        return false;
    }

    CallFrame* frame = &vm->frames[vm->frame_count++];
    frame->chunk = chunk;
    frame->ip = chunk->code;
    frame->slots = vm->stack_top - arg_count;

    return true;
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
            case OP_JUMP: {
                uint32_t offset = get_operand(instruction);
                frame->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint32_t offset = get_operand(instruction);
                Value condition = pop(vm);
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
                break;
            }
            case OP_CALL: {
                uint8_t arg_count = get_operand(instruction);

                Chunk* callee_chunk = (Chunk*)as_obj(pop(vm));

                if (!push_frame(vm, callee_chunk, arg_count)) {
                    return INTERPRET_RUNTIME_ERROR;
                }

                frame = &vm->frames[vm->frame_count - 1];
                break;
            }
            case OP_POP:
                pop(vm);
                break;
            case OP_RETURN: {
                Value result = pop(vm);

                vm->frame_count--;

                if (vm->frame_count == 0) {
                    if (is_int(result)) {
                        printf("Result: %d\n", as_int(result));
                    } else if (is_float(result)) {
                        printf("Result: %g\n", as_float(result));
                    } else if (is_bool(result)) {
                        printf("Result: %s\n", as_bool(result) ? "true" : "false");
                    }
                    return INTERPRET_OK;
                }
                vm->stack_top = frame->slots;

                push(vm ,result);
                frame = &vm->frames[vm->frame_count - 1];
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
    CallFrame* frame = &vm->frames[vm->frame_count++];
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
            printf("<obj %p>", as_obj(*slot));
        } else {
            printf("<unknown>");
        }
        printf(" ]");
    }
    printf("\n\n");
}
#endif
