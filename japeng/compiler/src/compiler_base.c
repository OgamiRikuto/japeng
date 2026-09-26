#include "compiler_internal.h"
#include "opcode.h"
#include <stdio.h>
#include <stdlib.h>

static int compile_args(Compiler* c, ASTNode* arg_node)
{
    if (arg_node == NULL) return 0;
    if (arg_node->kind == AST_STMT) {
        int count = 0;
        if (arg_node->stmt.left) count += compile_args(c, arg_node->stmt.left);
        if (arg_node->stmt.right) count += compile_args(c, arg_node->stmt.right);
        return count;
    }
    compile(c, arg_node);
    return 1;
}

// compiler/src/compiler_base.c

// 複合代入シンボルに対応する演算オペコードを解決
static Opcode resolve_compound_op(ObjString* msg) 
{
    if (msg == sym_plus_eq)  return OP_ADD;
    if (msg == sym_minus_eq) return OP_SUB;
    if (msg == sym_multi_eq) return OP_MUL;
    if (msg == sym_div_eq)   return OP_DIV;
    // 必要に応じて剰余 (sym_rem 等) も追加可能
    return (Opcode)-1;
}

// 複合代入のコード生成 (ローカル変数 or selfのフィールド)
bool compile_compound_assignment(Compiler* c, ASTNode* node) 
{
    ObjString* msg = node->send.message->identifier.name;
    Opcode op = resolve_compound_op(msg);
    if ((int)op == -1) {
        return false; // 複合代入ではない
    }

    ASTNode* target = node->send.receiver;
    if (!target || target->kind != AST_IDENTIFIER) {
        fprintf(stderr, "Error: Left side of compound assignment must be an identifier.\n");
        exit(EXIT_FAILURE);
    }

    ObjString* name = target->identifier.name;

    // パターン 1: ローカル変数の更新
    int slot = resolve_local(c, name);
    if (slot != -1) {
        emit_inst(c, OP_GET_LOCAL, (uint32_t)slot); // 1. 現在の値をロード
        compile(c, node->send.args);                 // 2. 右辺の加算値をロード
        emit_inst(c, op, 0);                         // 3. 演算実行 (OP_ADD 等)
        emit_inst(c, OP_SET_LOCAL, (uint32_t)slot);  // 4. ローカル変数に書き戻し
        
        return true;
    }

    // パターン 2: フィールド変数 (self) の更新
    if (c->current_class != NULL) {
        int field_idx = find_field_index(c->current_class, name);
        if (field_idx != -1) {
            // SET_FIELD 直前のスタックを [self, 新しい値] にするための積み順
            emit_inst(c, OP_GET_LOCAL, 0);                   // 1. SET_FIELD 用レシーバ (self)
            emit_inst(c, OP_GET_LOCAL, 0);                   // 2. GET_FIELD 用レシーバ (self)
            emit_inst(c, OP_GET_FIELD, (uint32_t)field_idx); // 3. 現在のフィールド値を取得
            compile(c, node->send.args);                      // 4. 右辺の加算値をロード
            emit_inst(c, op, 0);                              // 5. 演算実行
            emit_inst(c, OP_SET_FIELD, (uint32_t)field_idx);  // 6. self に書き戻し
            
            return true;
        }
    }

    fprintf(stderr, "Error: Undefined variable or field '%s' for compound assignment.\n", name->chars);
    exit(EXIT_FAILURE);
}

void compile_var_decl(Compiler* c, ASTNode* node)
{
    ObjString* var_name  = node->var_decl.identifier->identifier.name;
    ObjString* type_name = get_type_name(node->var_decl.type);
    TypeInfo* t_info = new_type_info(type_name, 1);

    int slot = add_local(c, var_name, t_info);

    if (is_integer_type(type_name)) {
        emit_constant(c, make_int(0));
    } else if (is_float_type(type_name)) {
        emit_constant(c, make_float(0.0));
    } else {
        uint32_t class_sym_idx = add_constant(c->chunk, make_obj((Obj*)type_name));
        emit_inst(c, OP_GET_GLOBAL, class_sym_idx);
        emit_inst(c, OP_NEW_INSTANCE, 0);
    }
    emit_inst(c, OP_SET_LOCAL, (uint32_t)slot);
}

void compile_assignment(Compiler* c, ASTNode* node)
{

    if (node->send.receiver->kind == AST_VAR_DECL) {
        ASTNode* decl = node->send.receiver;
        ObjString* var_name  = decl->var_decl.identifier->identifier.name;
        ObjString* type_name = get_type_name(decl->var_decl.type);
        TypeInfo* t_info = new_type_info(type_name, 1);

        int slot = add_local(c, var_name, t_info);


        if ((node->send.args != NULL && node->send.args->kind == AST_BLOCK) ||
            (type_name != NULL && strcmp(type_name->chars, "Function") == 0) ||
            is_float_type(type_name) || is_integer_type(type_name)) {
            compile(c, node->send.args);

            emit_inst(c, OP_SET_LOCAL, (uint32_t)slot);
            return;
        }


        uint32_t class_sym_idx = add_constant(c->chunk, make_obj((Obj*)type_name));
        emit_inst(c, OP_GET_GLOBAL, class_sym_idx);
        emit_inst(c, OP_NEW_INSTANCE, 0);

        int argc = 0;
        if (node->send.args != NULL) {
            emit_inst(c, OP_DUP, 0);
            argc = compile_args(c, node->send.args);
            uint32_t init_sym = add_constant(c->chunk, make_obj((Obj*)intern_cstr("init")));
            write_chunk(c->chunk, make_send_inst((uint16_t)argc, (uint16_t)init_sym));
            emit_inst(c, OP_POP, 0);
;       }
        emit_inst(c, OP_SET_LOCAL, (uint32_t)slot);
        return;
    }

    if (node->send.receiver->kind == AST_IDENTIFIER) {
        ObjString* target_name = node->send.receiver->identifier.name;
        int slot = resolve_local(c, target_name);
        if (slot != -1) {
            compile(c, node->send.args);
            emit_inst(c, OP_SET_LOCAL, (uint32_t)slot);
            emit_inst(c, OP_POP, 0);
            return;
        }

        if (c->current_class != NULL) {
            int field_idx = find_field_index(c->current_class, target_name);
            if (field_idx != -1) {
                emit_inst(c, OP_GET_LOCAL, 0);
                compile(c, node->send.args);
                emit_inst(c, OP_SET_FIELD, (uint32_t)field_idx);
                emit_inst(c, OP_POP, 0);
                return;
            }
        }

        fprintf(stderr, "Error: Assignment to undefined variable '%s'.\n", target_name->chars);
        exit(1);
        return;
    }
}

void compile_mesage_send(Compiler* c, ASTNode* node, ObjString* msg)
{
    bool is_self = (node->send.receiver->kind == AST_IDENTIFIER &&
                    node->send.receiver->identifier.name == sym_self);


    if (is_self) {
        int var_slot = resolve_local(c, msg);
        int upval_slot = (var_slot == -1) ? resolve_upvalue(c, msg) : -1;

        if (var_slot != -1 || upval_slot != -1) {

            if (var_slot != -1) {
                emit_inst(c, OP_GET_LOCAL, (uint32_t)var_slot);
            } else {
                emit_inst(c, OP_GET_UPVALUE, (uint32_t)upval_slot);
            }

            int argc = compile_args(c, node->send.args);

            uint32_t call_sym = add_constant(c->chunk, make_obj((Obj*)intern_cstr("call")));
            write_chunk(c->chunk, make_send_inst((uint16_t)argc, (uint16_t)call_sym));
            return;
        }
    }

    compile(c, node->send.receiver);
    int argc = compile_args(c, node->send.args);

    if (argc == 1) {
        if (msg == sym_plus) {emit_inst(c, OP_ADD, 0); return;}
        if (msg == sym_minus) {emit_inst(c, OP_SUB, 0); return;}
        if (msg == sym_multi) {emit_inst(c, OP_MUL, 0); return;}
        if (msg == sym_div) {emit_inst(c, OP_DIV, 0); return;}
        if (msg == sym_equal) {emit_inst(c, OP_EQUAL, 0); return;}
        if (msg == sym_less) {emit_inst(c, OP_LESS, 0); return;}
        if (msg == sym_gt) {emit_inst(c, OP_GREAT, 0); return;}
    }

    uint32_t sym_const = add_constant(c->chunk, make_obj((Obj*)msg));
    write_chunk(c->chunk, make_send_inst((uint16_t)argc, (uint16_t)sym_const));
}
