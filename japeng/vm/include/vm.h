#ifndef VM_H
#define VM_H

#include <stdint.h>

typedef struct chunk Chunk;
typedef uint64_t Value;

#define STACK_MAX 256

typedef enum {
    INTERPRET_OK,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

typedef struct vm{
    Chunk* chunk;
    uint32_t* ip;
    Value stack[STACK_MAX];
    Value* stack_top;
} VM;

void init_vm(VM* vm);
void free_vm(VM* vm);
InterpretResult interpret(VM* vm, Chunk* chunk);

void push(VM* vm, Value value);
Value pop(VM* vm);


#endif
