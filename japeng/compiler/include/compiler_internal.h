#ifndef COMPILER_INTERNAL_H
#define COMPILER_INTERNAL_H

#include "compiler.h"
#include "class.h"
#include "table.h"
#include "object.h"

// 共有シンボル
extern ObjString* sym_is;
extern ObjString* sym_are;
extern ObjString* sym_self;
extern ObjString* sym_if;
extern ObjString* sym_elif;
extern ObjString* sym_else;
extern ObjString* sym_repeat;
extern ObjString* sym_SInteger;
extern ObjString* sym_SFloat;
extern ObjString* sym_function;
extern ObjString* sym_plus;
extern ObjString* sym_minus;
extern ObjString* sym_multi;
extern ObjString* sym_div;
extern ObjString* sym_equal;
extern ObjString* sym_less;
extern ObjString* sym_gt;

#define DEBUG_CHUNK_WRITE 1
#define DEBUG_CLASS_WRITE 1
#define DEBUG_COMPILE_KIND 0
#define DEBUG_LOOP_WRITE 0

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

// 基本構文コンパイル関数
void compile_identifier_load(Compiler* compiler, ObjString* name);
void compile_var_decl(Compiler* c, ASTNode* node);
void compile_assignment(Compiler* c, ASTNode* node);
void compile_mesage_send(Compiler* c, ASTNode* node, ObjString* msg);

// 制御構文コンパイル関数
void compile_if(Compiler* c, ASTNode* node);
void compile_if_chain(Compiler* c, ASTNode* node);
void compile_repeat(Compiler* c, ASTNode* node);
void compile_break(Compiler* c);
void compile_continue(Compiler* c);
ObjFunction* compile_block(Compiler* parent, ASTNode* block_node);

// クラスコンパイル関数
TypeInfo* build_type_info(ASTNode* node);
void compile_class_def(Compiler* c, ASTNode* node);


#endif
