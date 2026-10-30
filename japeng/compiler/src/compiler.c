#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "compiler_internal.h"
#include "object.h"

static bool is_clause_msg(ASTNode* node, Symbol* sym)
{
    return (node != NULL && node->kind == AST_SEND &&
            node->send.message != NULL &&
            node->send.message->identifier.name != NULL &&
            node->send.message->identifier.name == sym);
}

static bool is_chain_clause(ASTNode* node)
{
    if (node == NULL) return false;
    if (node->kind == AST_SEND) {
        return is_clause_msg(node, sym_elif) || is_clause_msg(node, sym_else);
    }
    if (node->kind == AST_STMT) {
        return is_chain_clause(node->stmt.left);
    }
    return false;
}

static bool is_if_chain(ASTNode* node)
{
    if (node == NULL) return false;
    // 単体の if
    if (is_clause_msg(node, sym_if)) return true;

    // if から始まるチェイン (AST_STMT の左が if)
    if (node->kind == AST_STMT && is_clause_msg(node->stmt.left, sym_if)) {
        return is_chain_clause(node->stmt.right);
    }
    return false;
}

void compile_statement_node(Compiler* c, ASTNode* node) 
{
    if (node == NULL) return;

    if (node->kind == AST_STMT) {
        compile(c, node);
        return;
    }

    int depth_before = c->stack_depth;
    int locals_before = c->local_count;

    compile(c, node);

    // ★ 式文の末尾処理（余分な戻り値を捨てる）
    if (node->kind == AST_SEND && node->send.message != NULL) {

        // 宣言代入 (単数・複数) はローカル変数をスタックに残すため POP しない
        if (node->send.receiver != NULL && node->send.receiver->kind == AST_VAR_DECL) 
            return;

        // ★追加: 複数再代入 (a, b are ...) も内部ですでに各変数を POP 済みのため POP しない！
        if (node->send.receiver != NULL && node->send.receiver->kind == AST_STMT)
            return;

        Symbol* msg = node->send.message->identifier.name;
        // if や repeat は内部でスタックを自前制御するので除外
        if (msg != sym_if && msg != sym_repeat && 
            msg != sym_switch && msg != sym_each) emit_inst(c, OP_POP, 0);
    }

    int expected_increase = c->local_count - locals_before;
    int actual_increase = c->stack_depth - depth_before;

    if (actual_increase != expected_increase) {
        fprintf(stderr, "\033[1;31m[スタック異常検知] %s:%d: "
                        "期待される増加: +%d, 実際の増加: +%d (スタック深さ: %d -> %d)\033[0m\n",
                node->loc.filename ? node->loc.filename : "unknown",
                node->loc.line, expected_increase, actual_increase,
                depth_before, c->stack_depth);
        // assert(actual_increase == expected_increase && "文の実行後にスタックのリークまたは過剰POPが発生しました！");
    }
}

void compile(Compiler* c, ASTNode* node)
{
    if (node == NULL) return;
#if DEBUG_COMPILE_KIND
    fprintf(stderr, "[COMPILE] kind=%d\n", node->kind);
#endif

    switch(node->kind) {
        // 値（式）
        case AST_LITERAL:
        case AST_IDENTIFIER:
        case AST_BLOCK:
            compile_expression(c, node);
            break;
        
        case AST_VAR_DECL:
            compile_var_decl(c, node);
            break;
        
        case AST_SEND: {
            if (is_if_chain(node)) {
                compile_if_chain(c, node);
                return;
            }
            Symbol* msg_sym = node->send.message->identifier.name;
            bool is_self = (node->send.receiver->kind == AST_IDENTIFIER &&
                            node->send.receiver->identifier.name == sym_self);

            if (compile_compound_assignment(c, node)) break;

            // AST_SEND 内の代入処理
            if (msg_sym == sym_is || msg_sym == sym_are) {
                compile_assignment(c, node);
                break;
            }
            /* if */
            if (is_self && msg_sym == sym_if) {
                compile_if(c, node);
                break;
            }
            /* repeat */
            if (msg_sym == sym_repeat) {
                compile_repeat(c, node);
                break;
            }
            /* switch */
            if (is_self && msg_sym == sym_switch) {
                compile_switch(c, node);
                break;
            }

            /* each */
            if (msg_sym == sym_each) {
                compile_each(c, node);
                break;
            }

            // ★ 通常のメッセージ送信（メソッド呼び出し、演算子、ローカル関数）
            compile_mesage_send(c, node, msg_sym);
            break;
        }

        case AST_BREAK:
            compile_break(c, node);
            break;

        case AST_CONTINUE:
            compile_continue(c, node);
            break;

        case AST_RETURN: {
            ASTNode* exprs[16];
            int expr_count = 0;

            if (node->ret.expr != NULL) {
                collect_args(node->ret.expr, exprs, &expr_count, 16);
            }

            // =========================================================
            // ★ トップレベル (main.je 直下) の return: プロセスの終了コードを返す
            // =========================================================
            if (c->enclosing == NULL) {
                if (expr_count > 1) {
                    error_compile(node, ERR_ARITY_MISMATCH, 1, expr_count);
                    return;
                }

                if (expr_count == 1) {
                    TypeInfo* actual_type = compile_expression(c, exprs[0]);
                    TypeInfo* expected = new_type_info(sym_Integer, 0);

                    if (!is_assignable(c, expected, actual_type)) {
                        error_compile(exprs[0], ERR_RETURN_TYPE_MISMATCH,
                                      expected->name->chars, actual_type->name->chars);
                    }
                } else {
                    // 式なし return; は正常終了 0 扱い
                    emit_constant(c, make_int(0));
                }

                emit_inst(c, OP_RETURN, 1);
                break;
            }

            // =========================================================
            // ブロック (関数) 内の厳格チェック (既存コード)
            // =========================================================
            if (c->expected_ret_count == 1 && c->expected_ret_types[0]->name == sym_Void) {
                if (expr_count > 0) {
                    error_compile(node, ERR_RETURN_TYPE_MISMATCH, "Void (戻り値なし)", "値あり");
                }
            } else if (expr_count != c->expected_ret_count) {
                error_compile(node, ERR_ARITY_MISMATCH, c->expected_ret_count, expr_count);
            }

            if (expr_count == 0) {
                emit_inst(c, OP_RETURN, 0);
            } else {
                for (int i = 0; i < expr_count; i++) {
                    TypeInfo* actual_type = compile_expression(c, exprs[i]);
                    if (i < c->expected_ret_count) {
                        TypeInfo* expected = c->expected_ret_types[i];
                        if (expected != NULL && !is_assignable(c, expected, actual_type)) {
                            error_compile(exprs[i], ERR_RETURN_TYPE_MISMATCH,
                                          expected->name ? expected->name->chars : "unknown",
                                          actual_type ? actual_type->name->chars : "nil");
                        }
                    }
                }
                emit_inst(c, OP_RETURN, (uint32_t)expr_count);
            }
            break;
        }

        case AST_CLASS_DEF:
            compile_class_def(c, node);
            break;

        case AST_STMT:
            if (is_if_chain(node)) {
                compile_if_chain(c, node);
                return;
            }
            if (node->stmt.left)  compile_statement_node(c, node->stmt.left);
            if (node->stmt.right) compile_statement_node(c, node->stmt.right);
            break;

        default: 
            error_compile(node, ERR_UNHANDLED_AST_KIND, node->kind);
            break;
    }
}
