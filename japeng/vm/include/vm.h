#ifndef VM_H
#define VM_H

#include <stdint.h>

typedef struct chunk Chunk;
typedef uint64_t Value;
typedef struct table Table;

#define FRAME_MAX 64
#define STACK_MAX (FRAME_MAX * 256)

typedef struct callframe{
    Chunk* chunk;
    uint32_t* ip;
    Value* slots;
} CallFrame;

typedef struct vm{
    CallFrame frames[FRAME_MAX];
    int frame_count;

    Value stack[STACK_MAX];
    Value* stack_top;
    Table* globals;
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;


void init_vm(VM* vm);
void free_vm(VM* vm);
InterpretResult interpret(VM* vm, Chunk* chunk);

void push(VM* vm, Value value);
Value pop(VM* vm);

static inline Value peek(VM* vm, int distance) {
    return vm->stack_top[-1 - distance];
}

#endif
