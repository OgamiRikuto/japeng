#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler_internal.h"
#include "object.h"

static bool is_clause_msg(ASTNode* node, ObjString* sym)
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

    compile(c, node);

    // ★ 式文の末尾処理（余分な戻り値を捨てる）
    if (node->kind == AST_SEND && node->send.message != NULL) {

        if (node->send.receiver != NULL && node->send.receiver->kind == AST_VAR_DECL) 
            return;

        ObjString* msg = node->send.message->identifier.name;
        // if や repeat は内部でスタックを自前制御するので除外
        if (msg != sym_if && msg != sym_repeat) emit_inst(c, OP_POP, 0);
    }
}


// void compile(Compiler* c, ASTNode* node)
// {
//     if (node == NULL) return;
// #if DEBUG_COMPILE_KIND
//     fprintf(stderr, "[COMPILE] kind=%d\n", node->kind);
// #endif

//     switch(node->kind) {
//         case AST_LITERAL: {
//             emit_constant(c, node->literal.value);
//             break;
//         }

//         case AST_IDENTIFIER: {
//             int slot = resolve_local(c, node->identifier.name);
//             if (slot != -1) {
//                 emit_inst(c, OP_GET_LOCAL, (uint32_t)slot);
//                 break;
//             } 
//             int upval = resolve_upvalue(c, node->identifier.name);
//             if (upval != -1) {
//                 emit_inst(c, OP_GET_UPVALUE, (uint32_t)upval);
//                 break;
//             }
//             if (c->current_class != NULL) {
//                 int field_idx = find_field_index(c->current_class, node->identifier.name);
//                 if (field_idx != -1) {
//                     emit_load_self(c);
//                     emit_inst(c, OP_GET_FIELD, (uint32_t)field_idx);
//                     break;
//                 }
//             } 
//             error_compile(node, ERR_UNDEFINED_VAR, node->identifier.name->chars);
//             break;
//         }
        
//         case AST_VAR_DECL: {
//             compile_var_decl(c, node);
//             break;
//         }
        
//         case AST_SEND: {
//             if (is_if_chain(node)) {
//                 compile_if_chain(c, node);
//                 return;
//             }
//             ObjString* msg_sym = node->send.message->identifier.name;
//             bool is_self = (node->send.receiver->kind == AST_IDENTIFIER &&
//                             node->send.receiver->identifier.name == sym_self);
//             if (compile_compound_assignment(c, node)) break;
//             // AST_SEND 内の代入処理
//             if (msg_sym == sym_is || msg_sym == sym_are) {
//                 compile_assignment(c, node);
//                 break;
//             }
//             /* if */
//             if (is_self && msg_sym == sym_if) {
//                 compile_if(c, node);
//                 break;
//             }
//             /* repeat */
//             if (msg_sym == sym_repeat) {
//                 compile_repeat(c, node);
//                 break;
//             }
//             compile_mesage_send(c, node, msg_sym);
//             break;
//         }

//         case AST_BREAK: {
//             compile_break(c, node);
//             break;
//         }

//         case AST_CONTINUE: {
//             compile_continue(c, node);
//             break;
//         }

//         case AST_RETURN: {
//             TypeInfo* actual_ret = NULL;

//             if (node->ret.expr != NULL) {
//                 actual_ret = compile_expression(c, node->ret.expr);
//             } else {
//                 emit_constant(c, make_int(0));
//                 actual_ret = new_type_info(sym_Void, 0); // ★ 単なる return; は Void を返す扱い
//             }

//             if (c->expected_ret_type != NULL) {
//                 // ★ expected が Void なのに値を返そうとした場合の個別エラー（親切メッセージ）
//                 if (c->expected_ret_type->name == sym_Void && node->ret.expr != NULL) {
//                     error_compile(node, ERR_RETURN_TYPE_MISMATCH, "Void (戻り値なし)", actual_ret->name->chars);
//                 } else if (!is_assignable(c, c->expected_ret_type, actual_ret)) {
//                     error_compile(node, ERR_RETURN_TYPE_MISMATCH,
//                                   c->expected_ret_type->name->chars,
//                                   actual_ret ? actual_ret->name->chars : "nil");
//                 }
//             }

//             emit_inst(c, OP_RETURN, 0);
//             break;
//         }
//         case AST_CLASS_DEF: {
//             compile_class_def(c, node);
//             break;
//         }
//         case AST_STMT: {
//             if (is_if_chain(node)) {
//                 compile_if_chain(c, node);
//                 return;
//             }
//             if (node->stmt.left)  compile_statement_node(c, node->stmt.left);
//             if (node->stmt.right) compile_statement_node(c, node->stmt.right);
//             break;
//         }

//         case AST_BLOCK: {
//             ObjFunction* fn = compile_block(c, node);
//             uint32_t fn_idx = add_constant(c->chunk, make_obj((Obj*)fn));
//             if (fn->upvalue_count > 0) {
//                 emit_inst(c, OP_CLOSURE, fn_idx);
//             } else {
//                 emit_constant(c, make_obj((Obj*)fn));
//             }
//             break;
//         }

//         default: 
//             error_compile(node, ERR_UNHANDLED_AST_KIND, node->kind);
//             break;
//     }
// }
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
            ObjString* msg_sym = node->send.message->identifier.name;
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
            TypeInfo* actual_ret = NULL;

            if (node->ret.expr != NULL) {
                actual_ret = compile_expression(c, node->ret.expr);
            } else {
                emit_constant(c, make_int(0));
                actual_ret = new_type_info(sym_Void, 0);
            }

            if (c->expected_ret_type != NULL) {
                if (c->expected_ret_type->name == sym_Void && node->ret.expr != NULL) {
                    error_compile(node, ERR_RETURN_TYPE_MISMATCH, "Void (戻り値なし)", actual_ret->name->chars);
                } else if (!is_assignable(c, c->expected_ret_type, actual_ret)) {
                    error_compile(node, ERR_RETURN_TYPE_MISMATCH,
                                  c->expected_ret_type->name->chars,
                                  actual_ret ? actual_ret->name->chars : "nil");
                }
            }

            emit_inst(c, OP_RETURN, 0);
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
