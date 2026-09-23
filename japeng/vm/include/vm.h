#ifndef VM_H
#define VM_H

#include <stdint.h>

typedef struct chunk Chunk;
typedef uint64_t Value;
typedef struct table Table;
typedef struct objClass ObjClass;
typedef struct objClosure ObjClosure;

#define FRAME_MAX 64
#define STACK_MAX (FRAME_MAX * 256)

typedef struct callframe{
    ObjClosure* closure;
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

    // 組み込み関数
    ObjClass* class_object;
    ObjClass* class_integer;
    ObjClass* class_float;
    ObjClass* class_string;
    ObjClass* class_list;
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

ObjClass* get_class_for_value(VM* vm, Value val);

static inline Value peek(VM* vm, int distance) {
    return vm->stack_top[-1 - distance];
}

#endif
