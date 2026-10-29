#include "compiler_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

void init_compiler(Compiler* compiler, Chunk* chunk)
{
    compiler->enclosing = NULL;
    compiler->chunk = chunk;
    compiler->output_dir = ".";
    compiler->is_method = false;
    compiler->stack_depth = 0;
    memset(compiler->expected_ret_types, 0, sizeof(compiler->expected_ret_types));
    compiler->expected_ret_count = 0;
    compiler->local_count = 0;
    compiler->scope_depth = 0;

    compiler->locals[0].name = sym_self;
    compiler->locals[0].type = new_type_info(sym_Object, 0);
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
        case OP_CONCAT:
        case OP_ADD_INT:
        case OP_SUB_INT:
        case OP_MUL_INT:
        case OP_DIV_INT:
        case OP_REM_INT:
        case OP_ADD_FLOAT:
        case OP_SUB_FLOAT:
        case OP_MUL_FLOAT:
        case OP_DIV_FLOAT:
        case OP_ADD_DEC:
        case OP_SUB_DEC:
        case OP_MUL_DEC:
        case OP_DIV_DEC:
        case OP_LESS:
        case OP_GREAT:
        case OP_EQUAL:
        case OP_NEQUAL:
        case OP_NGREAT:
        case OP_NLESS:
        case OP_EQUAL_INT:
        case OP_NEQUAL_INT:
        case OP_LESS_INT:
        case OP_GREAT_INT:
        case OP_NLESS_INT:
        case OP_NGREAT_INT:
        case OP_SHL:
        case OP_SHR:
        case OP_BAND:
        case OP_BOR:
        case OP_BXOR:
        case OP_SET_FIELD: // receiver と value を pop して value を push
        case OP_INDEX_GET:
            c->stack_depth--;
            break;

        // --- ±0: スタック高さが変わらない命令 ---
        case OP_BNOT:
        case OP_NOT:
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
        case OP_INVOKE_VIRTUAL:
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
            c->stack_depth -= operand;
            break;
            
        case OP_INDEX_SET:
            c->stack_depth -= 2;
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

int resolve_local(Compiler* c, Symbol* name)
{
    for (int index = c->local_count - 1; index >= 0; index--) {
        if (c->locals[index].name == name) return index;
    }
    return -1;
}

int add_local(Compiler* c, Symbol* name, TypeInfo* type)
{
    if (c->local_count >= MAX_LOCALS) {
        Location no_loc = {0};
        error_at(no_loc, ERR_TOO_MANY_LOCALS);
        
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

int resolve_upvalue(Compiler* compiler, Symbol* name) 
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

Symbol* get_type_name(ASTNode* type_node) {
    if (type_node == NULL) return NULL;
    if (type_node->kind == AST_IDENTIFIER) {
        return type_node->identifier.name;
    }
    if (type_node->kind == AST_CLASS && type_node->class.classname != NULL) {
        return type_node->class.classname->identifier.name;
    }
    return NULL;
}

bool is_integer_type(Symbol* name) 
{
    if (name == NULL) return false;
    return (name == sym_Integer|| 
            name == sym_SmallInteger ||
            name == sym_BigInteger);
}

bool is_float_type(Symbol* name) 
{
    if (name == NULL) return false;
    return (name == sym_Float);
}

bool is_decimal_type(Symbol* name)
{
    if (name == NULL) return false;
    return (name == sym_Decimal);
}

bool is_list_type(Symbol* name)
{
    if (name == NULL) return false;
    return (name == sym_List);
}

bool is_string_type(Symbol* name) 
{
    if (name == NULL) return false;
    return (name == sym_String);
}

bool is_boolean_type(Symbol* name) 
{
    if (name == NULL) return false;
    return (name == sym_Boolean);
}

bool is_builtin(Symbol* name)
{
    if (name == NULL) return false;
    return  name == sym_Object ||
            name == sym_Integer ||
            name == sym_SmallInteger ||
            name == sym_BigInteger || 
            name == sym_Float ||
            name == sym_Decimal ||
            name == sym_List ||
            name == sym_String ||
            name == sym_Boolean ||
            name == sym_Void
            ;
}


Value parse_ast_literal_with_expected(ASTNode* node, TypeInfo* expected_type, TypeInfo** out_type)
{
    LiteralKind kind = node->literal.kind;
    Symbol* raw   = node->literal.raw;
    const char* str  = raw->chars;
    int len          = raw->length;

    // ★ 左辺で Decimal が明示されている場合: 整数・小数問わず ObjDecimal として生成
    if (expected_type != NULL && is_decimal_type(expected_type->name)) {
        if (kind == TOKEN_FLOAT || kind == TOKEN_INTEGER) {
            ObjDecimal* dec = new_decimal_from_string(str, len);
            if (out_type) *out_type = new_type_info(sym_Decimal, 0);
            return make_obj((Obj*)dec);
        }
    }

    if (expected_type != NULL && is_float_type(expected_type->name)) {
        if (kind == TOKEN_INTEGER) {
            char* end;
            double val = strtod(str, &end);
            if (out_type) *out_type = new_type_info(sym_Float, 0);
            return make_float(val);
        }
    }
        
    // --- 通常の評価 (Decimal 指定がない場合) ---
    switch (kind) {
        case TOKEN_INTEGER: {
            size_t start = (str[0] == '+' || str[0] == '-') ? 1 : 0;
            size_t num_digits = len - start;

            if (num_digits <= 9) {
                int32_t val = (int32_t)strtol(str, NULL, 10);
                if (out_type) *out_type = new_type_info(sym_Integer, 0);
                return make_int(val);
            }

            if (num_digits <= 18) {
                char* end;
                int64_t val = strtoll(str, &end, 10);
                if (*end == '\0') {
                    if (val >= INT32_MIN && val <= INT32_MAX) {
                        if (out_type) *out_type = new_type_info(sym_Integer, 0);
                        return make_int((int32_t)val);
                    }
                    BigInteger* bi = bi_from_i64(val);
                    if (out_type) *out_type = new_type_info(sym_Integer, 0);
                    return make_obj((Obj*)bi);
                }
            }

            BigInteger* bi = new_bi_from_string(raw);
            if (out_type) *out_type = new_type_info(sym_Integer, 0);
            return make_obj((Obj*)bi);
        }

        case TOKEN_FLOAT: {
            char* end;
            double val = strtod(str, &end);

            // Float 限界超過は Decimal に逃がさずエラーとする
            if ((val == HUGE_VAL || val == -HUGE_VAL)) {
                error_compile(node, ERR_FLOAT_OVERFLOW, str);
                if (out_type) *out_type = new_type_info(sym_Error, 0);
                return make_nil();
            }

            if (out_type) *out_type = new_type_info(sym_Float, 0);
            return make_float(val);
        }

        case TOKEN_STRING: {
            if (out_type) *out_type = new_type_info(sym_String, 0);
            return make_obj((Obj*)raw);
        }

        case TOKEN_TRUE: {
            if (out_type) *out_type = new_type_info(sym_Boolean, 0);
            return make_bool(true);
        }

        case TOKEN_FALSE: {
            if (out_type) *out_type = new_type_info(sym_Boolean, 0);
            return make_bool(false);
        }

        case TOKEN_NULL: {
            if (out_type) *out_type = new_type_info(sym_Null, 0);
            return make_nil();
        }

        // case TOKEN_BINARY: {
        //     // 0b...
        //     long long val = strtoll(str + 2, NULL, 2);
        //     if (out_type) *out_type = new_type_info(sym_Integer, 0);
        //     return (val >= INT32_MIN && val <= INT32_MAX) ? make_int((int32_t)val) : make_obj((Obj*)bi_from_i64(val));
        // }

        // case TOKEN_HEX: {
        //     // 0x...
        //     long long val = strtoll(str + 2, NULL, 16);
        //     if (out_type) *out_type = new_type_info(sym_Integer, 0);
        //     return (val >= INT32_MIN && val <= INT32_MAX) ? make_int((int32_t)val) : make_obj((Obj*)bi_from_i64(val));
        // }

        default:
            if (out_type) *out_type = new_type_info(sym_Error, 0);
            return make_nil();
    }
}
