#include "compiler_internal.h"
#include <stdio.h>
#include <stdlib.h>

ObjString* sym_is       = NULL;
ObjString* sym_are      = NULL;
ObjString* sym_self     = NULL;
ObjString* sym_if       = NULL;
ObjString* sym_elif     = NULL;
ObjString* sym_else     = NULL;
ObjString* sym_repeat   = NULL;
ObjString* sym_SInteger = NULL;
ObjString* sym_SFloat   = NULL;
ObjString* sym_function = NULL;
ObjString* sym_plus     = NULL;
ObjString* sym_minus    = NULL;
ObjString* sym_multi    = NULL;
ObjString* sym_div      = NULL;
ObjString* sym_equal    = NULL;
ObjString* sym_less     = NULL;
ObjString* sym_gt       = NULL;


void init_compiler_symbols(void) {
    if (!sym_is)        sym_is       = intern_cstr("is");
    if (!sym_are)       sym_are      = intern_cstr("are");
    if (!sym_self)      sym_self     = intern_cstr("self");
    if (!sym_if)        sym_if       = intern_cstr("if");
    if (!sym_elif)      sym_elif     = intern_cstr("elif");
    if (!sym_else)      sym_else     = intern_cstr("else");
    if (!sym_repeat)    sym_repeat   = intern_cstr("repeat"); 
    if (!sym_SInteger)  sym_SInteger = intern_cstr("SmallInteger"); 
    if (!sym_SFloat)    sym_SFloat   = intern_cstr("SmallFloat");
    if (!sym_function)  sym_function = intern_cstr("Function");
    if (!sym_plus)      sym_plus     = intern_cstr("+");
    if (!sym_minus)     sym_minus    = intern_cstr("-");
    if (!sym_multi)     sym_multi    = intern_cstr("*");
    if (!sym_div)       sym_div      = intern_cstr("/");
    if (!sym_equal)     sym_equal    = intern_cstr("=");
    if (!sym_less)      sym_less     = intern_cstr("<");
    if (!sym_gt)        sym_gt       = intern_cstr(">");
}

void init_compiler(Compiler* compiler, Chunk* chunk)
{
    compiler->enclosing = NULL;
    compiler->chunk = chunk;
    compiler->local_count = 0;
    compiler->scope_depth = 0;

    compiler->locals[0].name = intern_cstr("self");
    compiler->locals[0].depth = 1;
    compiler->local_count = 1;
    compiler->upvalue_count = 0;
    compiler->current_loop = NULL;
    compiler->current_class = NULL;
    compiler->defined_class = new_table(16);
}

void emit_inst(Compiler* c, Opcode op, uint32_t operand)
{
    write_chunk(c->chunk, make_inst(op, operand));
#if DEBUG_CHUNK_WRITE
    printf("[EMIT] count=%d, op=%d, operand=%u\n", c->chunk->count, op, operand);
#endif
}

void emit_constant(Compiler* c, Value val)
{
    uint32_t idx = add_constant(c->chunk, val);
    emit_inst(c, OP_CONSTANT, idx);
}

int emit_jump(Compiler* c, Opcode op)
{
    emit_inst(c, op, 0xFFFFF); // プレースホルダー (20bit/24bit)
    return c->chunk->count - 1;
}

void patch_jump(Compiler* c, int jump_inst_index)
{
    int offset = c->chunk->count - jump_inst_index - 1;
    Opcode op = get_op(c->chunk->code[jump_inst_index]);
    c->chunk->code[jump_inst_index] = make_inst(op, (uint32_t)offset);
}

void emit_loop(Compiler* c, int loop_start_index)
{
    int offset = c->chunk->count - loop_start_index + 1;
    emit_inst(c, OP_LOOP, (uint32_t)offset);
}

int resolve_local(Compiler* c, ObjString* name)
{
    for (int index = c->local_count - 1; index >= 0; index--) {
        if (c->locals[index].name == name) return index;
    }
    return -1;
}

int add_local(Compiler* c, ObjString* name, TypeInfo* type)
{
    if (c->local_count >= MAX_LOCALS) {
        fprintf(stderr, "Error: Too many local variables.\n");
        exit(EXIT_FAILURE);
    }

    int slot = c->local_count++;
    c->locals[slot].name = name;
    c->locals[slot].type = type;
    c->locals[slot].depth = c->scope_depth;
    return slot; 
}

int add_anonymous_local(Compiler* c)
{
    if (c->local_count >= MAX_LOCALS) {
        fprintf(stderr, "Error: Too many local variables.\n");
        exit(EXIT_FAILURE);
    }

    int slot = c->local_count++;
    c->locals[slot].name = NULL;
    c->locals[slot].depth = c->scope_depth;
    return slot;
}

int add_upvalue(Compiler* compiler, uint8_t index, bool is_local) 
{
    int count = compiler->upvalue_count;

    // 既にキャプチャ済みかチェック
    for (int i = 0; i < count; i++) {
        CompilerUpvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->is_local == is_local) {
            return i;
        }
    }

    // 新規登録
    compiler->upvalues[count].is_local = is_local;
    compiler->upvalues[count].index = index;
    return compiler->upvalue_count++;
}

int resolve_upvalue(Compiler* compiler, ObjString* name) 
{
    if (compiler->enclosing == NULL) return -1; // 最上位なら存在しない

    // 1. 親スコープのローカル変数にあるか？
    int local = resolve_local(compiler->enclosing, name);
    if (local != -1) {
        return add_upvalue(compiler, (uint8_t)local, true);
    }

    // 2. 親スコープも外側からキャプチャしているか？（再帰探索）
    int upvalue = resolve_upvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return add_upvalue(compiler, (uint8_t)upvalue, false);
    }

    return -1; // 外側のローカル変数としては見つからなかった
}
ObjString* get_type_name(ASTNode* type_node) {
    if (type_node == NULL) return NULL;
    if (type_node->kind == AST_IDENTIFIER) {
        return type_node->identifier.name;
    }
    if (type_node->kind == AST_CLASS && type_node->class.classname != NULL) {
        return type_node->class.classname->identifier.name;
    }
    return NULL;
}

bool is_integer_type(ObjString* name) {
    if (name == NULL) return false;
    return (strcmp(name->chars, "Integer") == 0 || 
            strcmp(name->chars, "SmallInteger") == 0);
}

bool is_float_type(ObjString* name) {
    if (name == NULL) return false;
    return (strcmp(name->chars, "Float") == 0 || 
            strcmp(name->chars, "SmallFloat") == 0);
}
