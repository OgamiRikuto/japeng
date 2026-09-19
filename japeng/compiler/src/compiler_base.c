#include "compiler_internal.h"
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

void compile_var_decl(Compiler* c, ASTNode* node)
{
    ObjString* var_name  = node->var_decl.identifier->identifier.name;
    ObjString* type_name = get_type_name(node->var_decl.type);
    TypeInfo* t_info = new_type_info(type_name, 1);

    // 1. 変数スロットを確保
    add_local(c, var_name, t_info);

    // 2. デフォルトコンストラクタの実行（引数 0 個）
    // if (type_name == sym_SInteger) {
    if (is_integer_type(type_name)) {
        // Integer のデフォルト: 0 (ヒープ確保なし)
        emit_constant(c, make_int(0));
    // } else if (type_name == sym_SFloat) {
    } else if (is_float_type(type_name)) {
        // Float のデフォルト: 0.0
        emit_constant(c, make_float(0.0));
    } else {
        // 一般クラス (Point など)
        // 2-1. クラスオブジェクトをスタックにロード
        uint32_t class_sym_idx = add_constant(c->chunk, make_obj((Obj*)type_name));
        emit_inst(c, OP_GET_GLOBAL, class_sym_idx);

        // 2-2. 引数 0 個でインスタンス生成 (引数なしコンストラクタを自動呼出)
        emit_inst(c, OP_NEW_INSTANCE, 0);
    }
}

void compile_assignment(Compiler* c, ASTNode* node)
{
    // パターン 1: 初期化付き宣言
    if (node->send.receiver->kind == AST_VAR_DECL) {
        ASTNode* decl = node->send.receiver;
        ObjString* var_name  = decl->var_decl.identifier->identifier.name;
        ObjString* type_name = get_type_name(decl->var_decl.type);
        TypeInfo* t_info = new_type_info(type_name, 1);

        int slot = add_local(c, var_name, t_info);

        // 即値型（Integer, Float 等）の場合
        // if (type_name == sym_SInteger || type_name == sym_SFloat) {
        if (is_float_type(type_name) || is_integer_type(type_name)) {
            compile(c, node->send.args); // 右辺の式を評価 (スタックに乗る)
            // スタックトップにある値がそのまま slot の領域になるため、
            // その場で作るなら OP_SET_LOCAL すら不要（既にスタックトップにある）
            // 明示的に書き込むなら:
            emit_inst(c, OP_SET_LOCAL, (uint32_t)slot);
            return;
        }

        // 一般クラス (Point 等) の場合はインスタンス生成命令へ分岐...
        uint32_t class_sym_idx = add_constant(c->chunk, make_obj((Obj*)type_name));
        emit_inst(c, OP_GET_GLOBAL, class_sym_idx);

        // 2. 引数を評価してスタックに積む (例: 10, 2)
        int argc = compile_args(c, node->send.args);

        // 3. インスタンス生成 & コンストラクタ呼び出し
        emit_inst(c, OP_NEW_INSTANCE, (uint32_t)argc);

        // 4. 生成されたインスタンスを変数スロットに格納
        emit_inst(c, OP_SET_LOCAL, (uint32_t)slot);
        return;
    }

    // パターン 2: 既存変数への再代入
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
    // 通常のメッセージ送信
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
