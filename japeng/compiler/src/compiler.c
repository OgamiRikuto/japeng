#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler_internal.h"
#include "object.h"

void compile(Compiler* c, ASTNode* node)
{
    if (node == NULL) return;
    printf("[COMPILE] kind=%d\n", node->kind);

    switch(node->kind) {
        case AST_LITERAL: {
            emit_constant(c, node->literal.value);
            break;
        }

        case AST_IDENTIFIER: {
            int slot = resolve_local(c, node->identifier.name);
            if (slot != -1) {
                emit_inst(c, OP_GET_LOCAL, (uint32_t)slot);
            } else {
                fprintf(stderr, "Error: Undefined variable '%s'.\n", node->identifier.name->chars);
                exit(EXIT_FAILURE);
            }
            break;
        }
        
        case AST_VAR_DECL: {
            compile_var_decl(c, node);
            break;
        }
        
        case AST_SEND: {
            ObjString* msg_sym = node->send.message->identifier.name;
            bool is_self = (node->send.receiver->kind == AST_IDENTIFIER &&
                            node->send.receiver->identifier.name == sym_self);

            // 特殊ケース: 代入
            // AST_SEND 内の代入処理
            if (msg_sym == sym_is) {
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
            
            compile_mesage_send(c, node, msg_sym);
            break;
        }

        case AST_BREAK: {
            compile_break(c);
            break;
        }

        case AST_CONTINUE: {
            compile_continue(c);
            break;
        }

        case AST_RETURN: {
            if (node->ret.expr != NULL) {
                compile(c, node->ret.expr);
            } else {
                emit_constant(c, make_int(0));
            }
            emit_inst(c, OP_RETURN, 0);
            break;
        }

        case AST_STMT: {
            if (node->stmt.left)  compile(c, node->stmt.left);
            if (node->stmt.right) compile(c, node->stmt.right);
            break;
        }

        case AST_BLOCK: {
            ObjFunction* fn = compile_block(c, node);
            emit_constant(c, make_obj((Obj*)fn));
            break;
        }

        default: 
            fprintf(stderr, "Unhandled ASTNode kind: %d\n", node->kind);
            break;
    }
}
