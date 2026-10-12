#include "compiler_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void init_compiler(Compiler* compiler, Chunk* chunk)
{
    compiler->enclosing = NULL;
    compiler->chunk = chunk;
    compiler->is_method = false;
    compiler->stack_depth = 0;
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

void emit_inst(Compiler* c, Opcode op, uint32_t operand) {
    write_chunk(c->chunk, make_inst(op, operand));

    switch (op) {
        // --- +1: プッシュ系 ---
        case OP_CONSTANT:
        case OP_GET_LOCAL:
        case OP_GET_GLOBAL:
        case OP_DUP:
        case OP_CLOSURE:
        case OP_GET_UPVALUE:
            c->stack_depth++;
            break;

        // --- -1: ポップ・二項演算・フィールド代入 ---
        case OP_POP:
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_LESS:
        case OP_GREAT:
        case OP_EQUAL:
        case OP_NEQUAL:
        case OP_SET_FIELD: // receiver と value を pop して value を push
            c->stack_depth--;
            break;

        // --- ±0: スタック高さが変わらない命令 ---
        case OP_GET_FIELD:     // receiver を pop して field を push
        case OP_NEW_INSTANCE:  // class を pop して instance を push
        case OP_SET_LOCAL:     // peek して代入
        case OP_SET_GLOBAL:    // peek して代入
        case OP_SET_UPVALUE:   // peek して代入
        case OP_JUMP:
        case OP_JUMP_IF_FALSE: // 条件は peek して分岐
        case OP_LOOP:
            break;

        // --- 動的: 引数個数に応じて消費 ---
        case OP_SEND:
        case OP_SUPER_CALL:
            // レシーバ(1) + 引数(argc) を消費して結果(1) を残す
            c->stack_depth -= get_send_args(operand);
            break;

        case OP_CALL:
            // OP_CALL が引数数を持っている場合 (例: operand が argc)
            c->stack_depth -= operand;
            break;

        case OP_RETURN:
            // 関数の戻り値 (1) を消費して終了
            c->stack_depth--;
            break;

        case OP_MAX:
            break;
    }

    #if DEBUG_CHUNK_WRITE
    fprintf(stderr, "[EMIT] count=%d, op=%d, operand=%u\n", c->chunk->count, op, operand);
    #endif
    assert(c->stack_depth >= 0 && "スタックアンダーフローを検出しました！");
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
        Location no_loc = {0};
        error_at(no_loc, ERR_TOO_MANY_LOCALS);
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
        Location no_loc = {0};
        error_at(no_loc, ERR_TOO_MANY_LOCALS);
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
    if (compiler->enclosing == NULL) return -1; 

    int local = resolve_local(compiler->enclosing, name);
    if (local != -1) {
        return add_upvalue(compiler, (uint8_t)local, true);
    }

    int upvalue = resolve_upvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return add_upvalue(compiler, (uint8_t)upvalue, false);
    }

    return -1;
}

void emit_load_self(Compiler* c) 
{
    if (c->is_method) {
        // 通常のメソッド内: スロット 0 が self
        emit_inst(c, OP_GET_LOCAL, 0);
    } else {
        // ネストしたブロック内: 外側のスコープから self をアップバリューとして取得
        int self_upval = resolve_upvalue(c, sym_self);
        emit_inst(c, OP_GET_UPVALUE, (uint32_t)self_upval);
    }
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

bool is_integer_type(ObjString* name) 
{
    if (name == NULL) return false;
    return (name == sym_Integer|| 
            name == sym_SmallInteger);
}

bool is_float_type(ObjString* name) 
{
    if (name == NULL) return false;
    return (name == sym_Float || 
            name == sym_SmallFloat);
}

bool is_list_type(ObjString* name)
{
    if (name == NULL) return false;
    return (name == sym_List);
}

bool is_string_type(ObjString* name) 
{
    if (name == NULL) return false;
    return (name == sym_String);
}

bool is_builtin(ObjString* name)
{
    if (name == NULL) return false;
    return  name == sym_Object ||
            name == sym_Integer ||
            name == sym_SmallInteger ||
            name == sym_Float ||
            name == sym_SmallFloat || 
            name == sym_List ||
            name == sym_String
            ;
}
