#ifndef COMPILER_H
#define COMPILER_H

#include <stdbool.h>
#include <stdint.h>
#include "ast.h"
#include "chunk.h"
#include "opcode.h"

#define MAX_LOCALS 256

typedef struct loop {
    struct loop* enclosing;
    int start_pos;
    int break_jump;
} Loop;

typedef struct local {
    ObjString* name;
    int depth;
} Local;

typedef struct compiler {
    struct compiler* enclosing;
    Chunk* chunk;

    Local locals[MAX_LOCALS];
    int local_count;
    int scope_depth;
    Loop* current_loop;
} Compiler;

void init_compiler_symbols(void);
void init_compiler(Compiler* compiler, Chunk* chunk);
void compile(Compiler* compiler, ASTNode* node);

int emit_jump(Compiler* c, Opcode op);
void patch_jump(Compiler* c, int jump_inst_index);
void emit_loop(Compiler* c, int loop_start_index);

#endif
