#ifndef COMPILER_H
#define COMPILER_H

#include <stdbool.h>
#include <stdint.h>
#include "ast.h"
#include "chunk.h"
#include "opcode.h"
#include "class.h"

#define MAX_TYPE_PARAMS 8
#define MAX_LOCALS 256

typedef struct loop {
    struct loop* enclosing;
    int start_pos;
    int break_jump;
    int continue_jump;
} Loop;

typedef struct local {
    Symbol* name;
    TypeInfo* type;
    int depth;
} Local;

typedef struct upvalue {
    uint8_t index;
    bool is_local;
} CompilerUpvalue;

typedef struct {
    Symbol* params[MAX_TYPE_PARAMS];
    TypeInfo*  args[MAX_TYPE_PARAMS];
    int        count;
} TypeEnv;

typedef struct compiler {
    struct compiler* enclosing;
    Chunk* chunk;
    const char* output_dir;
    bool is_method;
    int stack_depth;

    TypeInfo* expected_ret_types[16];
    int expected_ret_count;

    Local locals[MAX_LOCALS];
    int local_count;
    int scope_depth;
    
    CompilerUpvalue upvalues[MAX_LOCALS];
    int upvalue_count;

    Loop* current_loop;
    ClassDef* current_class;
    Table* defined_class;
} Compiler;

void init_compiler_symbols(void);
void init_compiler(Compiler* compiler, Chunk* chunk);
void compile(Compiler* compiler, ASTNode* node);
void emit_inst(Compiler* c, Opcode op, uint32_t operand);
void emit_constant(Compiler* c, Value val);

void declare_class_skeleton(Compiler* c, ASTNode* node);
void resolve_class_skeleton_relations(Compiler* c, ASTNode* node);
void declare_class_signatures(Compiler* c, ASTNode* node);
void compile_class_body(Compiler* c, ASTNode* node);
void compile_class_def(Compiler* c, ASTNode* node);


#endif
