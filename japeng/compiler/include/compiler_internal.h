#ifndef COMPILER_INTERNAL_H
#define COMPILER_INTERNAL_H

#include "compiler.h"
#include "object.h"

// 共有シンボル
extern ObjString* sym_is;
extern ObjString* sym_self;
extern ObjString* sym_if;
extern ObjString* sym_repeat;
extern ObjString* sym_SInteger;
extern ObjString* sym_SFloat;
extern ObjString* sym_plus;
extern ObjString* sym_minus;
extern ObjString* sym_multi;
extern ObjString* sym_div;
extern ObjString* sym_equal;
extern ObjString* sym_less;
extern ObjString* sym_gt;

// コード生成ヘルパー
void emit_inst(Compiler* c, Opcode op, uint32_t operand);
void emit_constant(Compiler* c, Value val);
int  emit_jump(Compiler* c, Opcode op);
void patch_jump(Compiler* c, int jump_inst_index);
void emit_loop(Compiler* c, int loop_start_index);

// 変数管理ヘルパー
int resolve_local(Compiler* c, ObjString* name);
int add_local(Compiler* c, ObjString* name);
int add_anonymous_local(Compiler* c);
// ObjString* get_type_name(ASTNode* type_node);
// bool is_integer_type(ObjString* name);
// bool is_float_type(ObjString* name);

// 基本構文コンパイル関数
void compile_var_decl(Compiler* c, ASTNode* node);
void compile_assignment(Compiler* c, ASTNode* node);
void compile_mesage_send(Compiler* c, ASTNode* node, ObjString* msg);

// 制御構文コンパイル関数（compiler_control.c 実装）
void compile_if(Compiler* c, ASTNode* node);
void compile_repeat(Compiler* c, ASTNode* node);
void compile_break(Compiler* c);
void compile_continue(Compiler* c);
ObjFunction* compile_block(Compiler* parent, ASTNode* block_node);

#endif
