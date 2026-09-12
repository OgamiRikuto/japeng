#include "vm.h"
#include "common.h"
#include <stdio.h>

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

static InterpretResult run(VM* vm)
{
    for(;;) {
        uint32_t instruction = *vm->ip++;
        Opcode op = get_op(instruction);

        switch(op) {
            case OP_CONSTANT: {
                uint32_t index = get_operand(instruction);
                Value constant = vm->chunk->constants[index];
                push(vm, constant);
                break;
            }
            case OP_POP:
                pop(vm);
                break;
            case OP_RETURN: {
                Value result = pop(vm);
                if (is_int(result)) {
                    printf("Result: %d\n", as_int(result));
                } else if (is_float(result)) {
                    printf("Result: %g\n", as_float(result));
                }
                return INTERPRET_OK;
            }
            default: 
                fprintf(stderr, "Unknown opcode: %d\n", op);
                return INTERPRET_RUNTIME_ERROR;
        }
    }
}

InterpretResult interpret(VM* vm, Chunk* chunk)
{
    vm->chunk = chunk;
    vm->ip = vm->chunk->code;
    return run(vm);
}
