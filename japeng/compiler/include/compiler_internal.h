#ifndef COMPILER_INTERNAL_H
#define COMPILER_INTERNAL_H

#include "compiler.h"
#include "class.h"
#include "table.h"
#include "object.h"
#include "error.h"

#define DEBUG_CHUNK_WRITE 0
#define DEBUG_CLASS_WRITE 0
#define DEBUG_COMPILE_KIND 0
#define DEBUG_LOOP_WRITE 0

#define error_compile(node, code, ...) \
    error_at((node)->loc, (code), ##__VA_ARGS__)

// コード生成ヘルパー
int  emit_jump(Compiler* c, Opcode op);
void patch_jump(Compiler* c, int jump_inst_index);
void emit_loop(Compiler* c, int loop_start_index);

// 変数管理ヘルパー
int resolve_local(Compiler* c, ObjString* name);
int add_local(Compiler* c, ObjString* name, TypeInfo* type);
int add_anonymous_local(Compiler* c);
int add_upvalue(Compiler* compiler, uint8_t index, bool is_local);
int resolve_upvalue(Compiler* compiler, ObjString* name);
ObjString* get_type_name(ASTNode* type_node);
bool is_integer_type(ObjString* name);
bool is_float_type(ObjString* name);
bool is_string_type(ObjString* name);

// 基本構文コンパイル関数
bool compile_compound_assignment(Compiler* c, ASTNode* node);
void compile_identifier_load(Compiler* compiler, ObjString* name);
void compile_var_decl(Compiler* c, ASTNode* node);
void compile_assignment(Compiler* c, ASTNode* node);
void compile_mesage_send(Compiler* c, ASTNode* node, ObjString* msg);

// 制御構文コンパイル関数
void compile_if(Compiler* c, ASTNode* node);
void compile_if_chain(Compiler* c, ASTNode* node);
void compile_repeat(Compiler* c, ASTNode* node);
void compile_break(Compiler* c, ASTNode* node);
void compile_continue(Compiler* c, ASTNode* node);
ObjFunction* compile_block(Compiler* parent, ASTNode* block_node);

// クラスコンパイル関数
TypeInfo* build_type_info(ASTNode* node);
void compile_class_def(Compiler* c, ASTNode* node);


#endif
