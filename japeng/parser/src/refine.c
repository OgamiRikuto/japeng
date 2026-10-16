#include "ast.h"
#include "common.h"
#include <string.h>
#include <stdbool.h>

// -----------------------------------------------------------------------------
// 1. 演算子シンボルから優先順位（数値が高いほど優先）を取得
// -----------------------------------------------------------------------------
static int get_binary_precedence(const ObjString* op) {
    if (!op) return 0;

    // 乗除・剰余
    if (op == sym_multi || op == sym_div || op == sym_rem) {
        return 60;
    }
    // 加減
    if (op == sym_plus || op == sym_minus) {
        return 50;
    }
    // ビットシフト
    if (op == sym_lshif || op == sym_rshif) {
        return 40;
    }
    // 大小比較
    if (op == sym_gt || op == sym_less || 
        op == sym_ngt || op == sym_nless) {
        return 30;
    }
    // 等値比較
    if (op == sym_equal || op == sym_nequal) {
        return 20;
    }
    // 代入系演算子
    if (op == sym_is || op == sym_are || 
        op == sym_plus_eq || op == sym_minus_eq ||
        op == sym_multi_eq || op == sym_div_eq) {
        return 10;
    }

    // ★ 通常のメソッド呼び出し (run1, println 等) は回転させない！
    return 0;
}

// ノードが二項演算子（単一引数を持つ演算子メッセージ送信）か判定
static bool is_binary_op_send(const ASTNode* node) {
    if (!node || node->kind != AST_SEND) return false;
    if (!node->send.receiver || !node->send.message || !node->send.args) return false;
    
    if (node->send.args->kind == AST_STMT) return false;
    if (node->send.message->kind != AST_IDENTIFIER) return false;

    // ★ 優先度が 0 より大きい（登録された演算子である）こと！
    ObjString* op = node->send.message->identifier.name;
    return get_binary_precedence(op) > 0;
}

// -----------------------------------------------------------------------------
// 2. 木の回転と優先順位の補正
// -----------------------------------------------------------------------------

// 二項演算ノードの木を優先順位に従って回転・組み替えるコア関数
static ASTNode* rotate_binary_op(ASTNode* node) {
    if (!is_binary_op_send(node)) return node;

    // 左辺（receiver）が二項演算ノードの場合のチェック
    // 構造: (A op_left B) op_root C
    ASTNode* left = node->send.receiver;
    if (is_binary_op_send(left)) {
        ObjString* op_root = node->send.message->identifier.name;
        ObjString* op_left = left->send.message->identifier.name;

        int prec_root = get_binary_precedence(op_root);
        int prec_left = get_binary_precedence(op_left);

        // 例: (1 + 2) * 3 のように、親（*）の方が左の子（+）よりも優先度が高い場合
        // 期待する構造: 1 + (2 * 3)
        if (prec_root > prec_left) {
            // ノードの付け替え（右回転）
            ASTNode* a = left->send.receiver;
            ASTNode* b = left->send.args;
            ASTNode* c = node->send.args;

            // node（*）のレシーバを B に、引数を C に付け替えて (B * C) を構成
            node->send.receiver = b;
            node->send.args = c;

            // left（+）のレシーバを A に、引数を (B * C) に付け替えて A + (B * C) を構成
            left->send.receiver = a;
            left->send.args = node;

            // 付け替えたサブツリーに対して再帰的にさらに回転が必要かチェック
            left->send.args = rotate_binary_op(left->send.args);
            return rotate_binary_op(left);
        }
    }

    return node;
}

// -----------------------------------------------------------------------------
// 3. 全 AST ノードを巡回するメインエントリー
// -----------------------------------------------------------------------------
ASTNode* fix_precedence(ASTNode* node) 
{
    if (!node) return NULL;

    switch (node->kind) {
        case AST_STMT:
            node->stmt.left = fix_precedence(node->stmt.left);
            node->stmt.right = fix_precedence(node->stmt.right);
            break;

        case AST_SEND:
            // まず子ノードをすべて再帰的に補正
            node->send.receiver = fix_precedence(node->send.receiver);
            node->send.message  = fix_precedence(node->send.message);
            node->send.args     = fix_precedence(node->send.args);

            // 二項演算メッセージであれば回転ロジックを適用
            node = rotate_binary_op(node);
            break;

        case AST_BLOCK:
            node->block.args = fix_precedence(node->block.args);
            node->block.rets = fix_precedence(node->block.rets);
            node->block.body = fix_precedence(node->block.body);
            break;

        case AST_CLASS_DEF:
            node->class_def.class = fix_precedence(node->class_def.class);
            node->class_def.super_class = fix_precedence(node->class_def.super_class);
            node->class_def.from_classes = fix_precedence(node->class_def.from_classes);
            node->class_def.members = fix_precedence(node->class_def.members);
            break;

        case AST_FIELD_DECL:
            node->field_decl.identifier = fix_precedence(node->field_decl.identifier);
            node->field_decl.type = fix_precedence(node->field_decl.type);
            node->field_decl.from_class = fix_precedence(node->field_decl.from_class);
            node->field_decl.default_data = fix_precedence(node->field_decl.default_data);
            break;

        case AST_VAR_DECL:
            node->var_decl.identifier = fix_precedence(node->var_decl.identifier);
            node->var_decl.type = fix_precedence(node->var_decl.type);
            break;

        case AST_RETURN:
            node->ret.expr = fix_precedence(node->ret.expr);
            break;

        // 葉ノード（リテラル、識別子、break/continue など）は何もしない
        case AST_LITERAL:
        case AST_IDENTIFIER:
        case AST_CLASS:
        case AST_BREAK:
        case AST_CONTINUE:
        default:
            break;
    }

    return node;
}

Value parse_ast_literal(ASTNode* node, TypeInfo** out_type);

// 左右が両方とも整数リテラルの場合、コンパイル時に計算して 1 つのリテラルノードに潰す
static ASTNode* try_fold_int_binary(ASTNode* node) {
    ASTNode* left  = node->send.receiver;
    ASTNode* right = node->send.args;
    ObjString* op  = node->send.message->identifier.name;

    // 両辺が TOKEN_INTEGER かチェック
    if (left->kind != AST_LITERAL || left->literal.kind != TOKEN_INTEGER) return node;
    if (right->kind != AST_LITERAL || right->literal.kind != TOKEN_INTEGER) return node;

    // 左右の値をパース
    Value v1 = parse_ast_literal(left, NULL);
    Value v2 = parse_ast_literal(right, NULL);

    // 左右が SmallInteger 同士の場合
    if (is_int(v1) && is_int(v2)) {
        int32_t a = as_int(v1);
        int32_t b = as_int(v2);
        int32_t res;
        bool overflow = false;

        if (op == sym_plus) {
            overflow = __builtin_add_overflow(a, b, &res);
        } else if (op == sym_minus) {
            overflow = __builtin_sub_overflow(a, b, &res);
        } else if (op == sym_multi) {
            overflow = __builtin_mul_overflow(a, b, &res);
        } else if (op == sym_div) {
            if (b == 0) return node; // ゼロ除算は畳み込まず実行時エラーに委ねる
            if (a == INT32_MIN && b == -1) overflow = true;
            else res = a / b;
        } else {
            return node; // 対象外演算子
        }

        // オーバーフローしなかった場合: 新しい文字列リテラルノードに差し替え
        if (!overflow) {
            char buf[64];
            int len = snprintf(buf, sizeof(buf), "%d", res);
            return create_literal_node(TOKEN_INTEGER, new_string(buf, len));
        }

        // オーバーフローした場合: 64bit で計算して BigInteger 文字列リテラルへ昇格！
        int64_t wide_res;
        if (op == sym_plus) wide_res = (int64_t)a + (int64_t)b;
        else if (op == sym_minus) wide_res = (int64_t)a - (int64_t)b;
        else if (op == sym_multi) wide_res = (int64_t)a * (int64_t)b;
        else wide_res = -(int64_t)INT32_MIN;

        char buf[64];
        int len = snprintf(buf, sizeof(buf), "%lld", wide_res);
        return create_literal_node(TOKEN_INTEGER, new_string(buf, len));
    }

    // 将来: 片方または両方が BigInteger だった場合も bi_add / bi_mul を呼んで畳み込み可能！
    return node;
}

ASTNode* fold_constants(ASTNode* node) 
{
    if (!node) return NULL;

    switch (node->kind) {
        case AST_STMT:
            node->stmt.left  = fold_constants(node->stmt.left);
            node->stmt.right = fold_constants(node->stmt.right);
            break;

        case AST_SEND:
            node->send.receiver = fold_constants(node->send.receiver);
            node->send.args     = fold_constants(node->send.args);

            // 二項演算メッセージであれば畳み込みを試行
            if (node->send.message && node->send.message->kind == AST_IDENTIFIER) {
                node = try_fold_int_binary(node);
            }
            break;

        case AST_BLOCK:
            node->block.body = fold_constants(node->block.body);
            break;

        case AST_RETURN:
            node->ret.expr = fold_constants(node->ret.expr);
            break;

        default:
            break;
    }

    return node;
}
