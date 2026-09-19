#include "compiler_internal.h"
#include <stdio.h>
#include <stdlib.h>

#define SENTINEL_JUMP 0xFFFFFF

static void start_loop(Compiler* c, Loop* loop, int start_pos)
{
    loop->enclosing = c->current_loop;
    loop->start_pos = start_pos;
    loop->break_jump = -1;
    c->current_loop = loop;
}

static void end_loop(Compiler* c, Loop* loop)
{
    int jump = loop->break_jump;
    printf("[END_LOOP] start patching, break_jump=%d\n", jump);
    while (jump != -1) {
        // オペランドに退避していた「前の break 位置」を取り出す (上位ビット)
        uint32_t raw_inst = c->chunk->code[jump];
        uint32_t next = get_operand(raw_inst);
        int next_jump = (next == SENTINEL_JUMP) ? -1 : (int)next;
        printf("  patching jump=%d, raw=0x%08X, next=%d\n", jump, raw_inst, next_jump);

        // 現在のアドレスへ脱出ジャンプをパッチ
        patch_jump(c, jump);
        jump = next_jump;
    }
    c->current_loop = loop->enclosing;
    printf("[END_LOOP] finished\n");
}

void compile_if(Compiler* c, ASTNode* node)
{
    ASTNode* cond_node = node->send.args->stmt.left;
    ASTNode* block_node = node->send.args->stmt.right;

    compile(c, cond_node);

    int then_jump = emit_jump(c, OP_JUMP_IF_FALSE);
    emit_inst(c, OP_POP, 0);

    compile(c, block_node->block.body);

    patch_jump(c, then_jump);
    emit_inst(c, OP_POP, 0);
}

void compile_repeat(Compiler* c, ASTNode* node)
{
    bool is_self = (node->send.receiver->kind == AST_IDENTIFIER &&
                    node->send.receiver->identifier.name == sym_self);
    // パターン 2-A: repeat (cond) [ body ] (条件ループ)
    if (is_self && node->send.args->kind == AST_STMT) {
        ASTNode* cond_node  = node->send.args->stmt.left;
        ASTNode* block_node = node->send.args->stmt.right;
    
        int loop_start = c->chunk->count;
    
        Loop loop;
        start_loop(c, &loop, loop_start);
    
        compile(c, cond_node);
        int exit_jump = emit_jump(c, OP_JUMP_IF_FALSE);
        emit_inst(c, OP_POP, 0);
    
        compile(c, block_node->block.body);
    
        emit_loop(c, loop_start);
        patch_jump(c, exit_jump);
        emit_inst(c, OP_POP, 0);
    
        end_loop(c, &loop);
        return;
    }
    
    // パターン 2-B: repeat [ body ] (無限ループ)
    if (is_self && node->send.args->kind == AST_BLOCK) {
        int loop_start = c->chunk->count;
    
        Loop loop;
        start_loop(c, &loop, loop_start);
    
        compile(c, node->send.args->block.body);
    
        emit_loop(c, loop_start);
    
        end_loop(c, &loop);
        return;
    }
    
    // パターン 2-C: limit repeat [ arg i: Integer. body ] (回数指定ループ)
    if (!is_self && node->send.args->kind == AST_BLOCK) {
        ASTNode* block_node = node->send.args;
    
        // 1. 上限値 (limit) を評価して匿名スロットに格納
        compile(c, node->send.receiver);
        int limit_slot = add_anonymous_local(c);
        emit_inst(c, OP_SET_LOCAL, limit_slot);
    
        // 2. カウンタ変数 (0 初期化)
        int counter_slot;
        if (block_node->block.args != NULL) {
            ASTNode* id_node = block_node->block.args->var_decl.identifier;
            ObjString* type_name = block_node->block.args->var_decl.identifier->identifier.name;
            counter_slot = add_local(c, id_node->identifier.name, new_type_info(type_name, 1));
        } else {
            counter_slot = add_anonymous_local(c);
        }
        emit_constant(c, make_int(0));
        emit_inst(c, OP_SET_LOCAL, counter_slot);
    
        // 3. ループ判定先頭
        int loop_start = c->chunk->count;
    
        Loop loop;
        start_loop(c, &loop, loop_start);
    
        // 条件判定: counter < limit
        emit_inst(c, OP_GET_LOCAL, counter_slot);
        emit_inst(c, OP_GET_LOCAL, limit_slot);
        emit_inst(c, OP_LESS, 0);
    
        int exit_jump = emit_jump(c, OP_JUMP_IF_FALSE);
        emit_inst(c, OP_POP, 0);
    
        // 4. 本体ブロック実行
        compile(c, block_node->block.body);
    
        // 5. カウンタ加算: counter = counter + 1
        emit_inst(c, OP_GET_LOCAL, counter_slot);
        emit_constant(c, make_int(1));
        emit_inst(c, OP_ADD, 0);
        emit_inst(c, OP_SET_LOCAL, counter_slot);
        emit_inst(c, OP_POP, 0);
    
        // 6. ループ巻き戻し & 脱出パッチ
        emit_loop(c, loop_start);
        patch_jump(c, exit_jump);
        emit_inst(c, OP_POP, 0);
    
        end_loop(c, &loop);
        return;
    }
}

void compile_break(Compiler* c)
{
    if (c->current_loop == NULL) {
        fprintf(stderr, "Error: 'break' outside of loop.\n");
        exit(EXIT_FAILURE);
    }
    int jump = emit_jump(c, OP_JUMP);
    uint32_t prev = (c->current_loop->break_jump == -1) 
                    ? SENTINEL_JUMP
                    : (uint32_t)c->current_loop->break_jump;
    // オペランド部分に前の break 位置を一時保存してリスト化
    c->chunk->code[jump] = make_inst(OP_JUMP, prev);
    c->current_loop->break_jump = jump;
}

void compile_continue(Compiler* c)
{
    if (c->current_loop == NULL) {
        fprintf(stderr, "Error: 'continue' outside of loop.\n");
        exit(EXIT_FAILURE);
    }
    emit_loop(c, c->current_loop->start_pos);
}

static void compile_block_args(Compiler* c, ASTNode* args_node, int* arity)
{
    if (args_node == NULL) return;
    if (args_node->kind == AST_STMT) {
        compile_block_args(c, args_node->stmt.left, arity);
        compile_block_args(c, args_node->stmt.right, arity);
        return;
    }
    if (args_node->kind == AST_VAR_DECL) {
        ObjString* arg_name = args_node->var_decl.identifier->identifier.name;
        TypeInfo* t_info = build_type_info(args_node->var_decl.type);
        add_local(c, arg_name, t_info);
        (*arity)++;
    }
}

ObjFunction* compile_block(Compiler* parent, ASTNode* block_node)
{
    Chunk* fn_chunk = (Chunk*)malloc(sizeof(Chunk));
    init_chunk(fn_chunk);

    Compiler block_compiler;
    init_compiler(&block_compiler, fn_chunk);
    block_compiler.enclosing = parent;
    block_compiler.current_class = parent->current_class;

    // if (block_compiler.current_class != NULL) {
    //     block_compiler.locals[0].type = block_compiler.current_class->type;
    // }

    int arity = 0;
    if (block_node->block.args != NULL) {
        compile_block_args(&block_compiler, block_node->block.args, &arity);
    }

    // TypeInfo* ret_type = NULL;
    // if (block_node->block.has_ret) {
    //     ret_type = build_type_info(block_node->block.rets);
    // }

    compile(&block_compiler, block_node->block.body);

    if (!block_node->block.has_ret)
        emit_inst(&block_compiler, OP_RETURN, 0);

    return new_function(fn_chunk, arity);
}
