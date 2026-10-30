#include "compiler_internal.h"
#include <stdio.h>
#include <stdlib.h>

#define SENTINEL_JUMP 0xFFFFFF

static void start_loop(Compiler* c, Loop* loop, int start_pos)
{
    loop->enclosing = c->current_loop;
    loop->start_pos = start_pos;
    loop->break_jump = -1;
    loop->continue_jump = -1;
    c->current_loop = loop;
}

static void end_loop(Compiler* c, Loop* loop)
{
    int jump = loop->break_jump;
#if DEBUG_LOOP_WRITE
    fprintf(stderr, "[END_LOOP] start patching, break_jump=%d\n", jump);
#endif
    while (jump != -1) {
        uint32_t raw_inst = c->chunk->code[jump];
        uint32_t next = get_operand(raw_inst);
        int next_jump = (next == SENTINEL_JUMP) ? -1 : (int)next;
#if DEBUG_LOOP_WRITE
        fprintf(stderr, "  patching jump=%d, raw=0x%08X, next=%d\n", jump, raw_inst, next_jump);
#endif
        patch_jump(c, jump);
        jump = next_jump;
    }
    c->current_loop = loop->enclosing;
#if DEBUG_LOOP_WRITE
    fprintf(stderr, "[END_LOOP] finished\n");
#endif
}

static void patch_continue_jumps(Compiler* c, Loop* loop)
{
    int jump = loop->continue_jump;
    while (jump != -1) {
        uint32_t raw_inst = c->chunk->code[jump];
        uint32_t next = get_operand(raw_inst);
        int next_jump = (next == SENTINEL_JUMP) ? -1 : (int)next;
        patch_jump(c, jump);
        jump = next_jump;
    }
    loop->continue_jump = -1;
}

static void collect_clauses(ASTNode* node, ASTNode** list, int* count, int max)
{
    if (node == NULL || *count >= max) return;
    if (node->kind == AST_STMT) {
        collect_clauses(node->stmt.left, list, count, max);
        collect_clauses(node->stmt.right, list, count, max);
    } else {
        list[(*count)++] = node;
    }
}

void compile_if_chain(Compiler* c, ASTNode* node)
{
    ASTNode* clauses[32];
    int clause_count = 0;
    collect_clauses(node, clauses, &clause_count, 32);

    int exit_jumps[32];
    int exit_count = 0;

    for (int i = 0; i < clause_count; i++) {
        ASTNode* clause = clauses[i];
        if (clause == NULL || clause->kind != AST_SEND || clause->send.message == NULL) {
            continue;
        }

        Symbol* msg = clause->send.message->identifier.name;

        if (msg == sym_if || msg == sym_elif) {
            ASTNode* cond_node  = clause->send.args->stmt.left;
            ASTNode* block_node = clause->send.args->stmt.right;

            // 1. 条件式の評価
            TypeInfo* cond_type = compile_expression(c, cond_node);
            if (cond_type != NULL && !is_boolean_type(cond_type->name) &&
                cond_type->name != sym_Auto && cond_type->name != sym_Error) {
                error_compile(cond_node, ERR_IF_COND_NOT_BOOL, msg->chars, cond_type->name->chars);
                return;
            }

            // 2. 偽なら次の節へジャンプ
            int false_jump = emit_jump(c, OP_JUMP_IF_FALSE);

            // 3. 真のパス: 条件値を POP してブロック実行
            emit_inst(c, OP_POP, 0);
            compile_statement_node(c, block_node->block.body);

            // 4. ブロック終了後は if 全体の末尾へ脱出
            exit_jumps[exit_count++] = emit_jump(c, OP_JUMP);

            // 5. 偽だったときの着地点をここ（次の節の直前）にパッチ
            patch_jump(c, false_jump);

            c->stack_depth++;

            // 6. 偽のパス: 条件値を POP (次の elif の評価や else の実行に備える)
            emit_inst(c, OP_POP, 0);

        } else if (msg == sym_else) {
            ASTNode* block_node = clause->send.args;
            compile_statement_node(c, block_node->block.body);
        }
    }

    // 7. 各節の末尾から飛んできたすべての OP_JUMP を現在の末尾アドレスにパッチ
    for (int i = 0; i < exit_count; i++) {
        patch_jump(c, exit_jumps[i]);
    }
}

void compile_if(Compiler* c, ASTNode* node)
{
    ASTNode* cond_node = node->send.args->stmt.left;
    ASTNode* block_node = node->send.args->stmt.right;

    TypeInfo* cond_type = compile_expression(c, cond_node);
    if (cond_type != NULL && !is_boolean_type(cond_type->name) &&
        cond_type->name != sym_Auto && cond_type->name != sym_Error) {
        error_compile(cond_node, ERR_IF_COND_NOT_BOOL, "if", cond_type->name->chars);
        return;
    }

    int then_jump = emit_jump(c, OP_JUMP_IF_FALSE);
    emit_inst(c, OP_POP, 0);

    compile_statement_node(c, block_node->block.body);

    int exit_jump = emit_jump(c, OP_JUMP);

    patch_jump(c, then_jump);
    c->stack_depth++;
    emit_inst(c, OP_POP, 0);
    patch_jump(c, exit_jump);
}

void compile_repeat(Compiler* c, ASTNode* node)
{
    bool is_self = (node->send.receiver->kind == AST_IDENTIFIER &&
                    node->send.receiver->identifier.name == sym_self);
    // パターン 1: repeat (cond) [ body ] (条件ループ)
    if (is_self && node->send.args->kind == AST_STMT) {
        ASTNode* cond_node  = node->send.args->stmt.left;
        ASTNode* block_node = node->send.args->stmt.right;
    
        int loop_start = c->chunk->count;
    
        Loop loop;
        start_loop(c, &loop, loop_start);
    
        TypeInfo* cond_type = compile_expression(c, cond_node);
        if (cond_type != NULL && !is_boolean_type(cond_type->name) &&
            cond_type->name != sym_Auto && cond_type->name != sym_Error) {
            error_compile(cond_node, ERR_REPEAT_COND_NOT_BOOL, cond_type->name->chars);
            return;
        }

        int exit_jump = emit_jump(c, OP_JUMP_IF_FALSE);
        emit_inst(c, OP_POP, 0);
    
        compile_statement_node(c, block_node->block.body);
    
        emit_loop(c, loop_start);
        patch_jump(c, exit_jump);
        emit_inst(c, OP_POP, 0);
    
        end_loop(c, &loop);
        return;
    }
    
    // パターン 2: repeat [ body ] (無限ループ)
    if (is_self && node->send.args->kind == AST_BLOCK) {
        int loop_start = c->chunk->count;
    
        Loop loop;
        start_loop(c, &loop, loop_start);
    
        compile_statement_node(c, node->send.args->block.body);
    
        emit_loop(c, loop_start);
    
        end_loop(c, &loop);
        return;
    }
    
    // パターン 3: limit repeat [ arg i: Integer. body ] (回数指定ループ)
    if (!is_self && node->send.args->kind == AST_BLOCK) {
        ASTNode* block_node = node->send.args;
    
        // 1. 上限値 (limit) を評価して匿名スロットに格納
        TypeInfo* limit_type = compile_expression(c, node->send.receiver);
        if (limit_type != NULL && !is_integer_type(limit_type->name) &&
            limit_type->name != sym_Auto && limit_type->name != sym_Error) {
            error_compile(node->send.receiver, ERR_REPEAT_LIMIT_NOT_INT, limit_type->name->chars);
            return;
        }
        int limit_slot = add_anonymous_local(c);
        emit_inst(c, OP_SET_LOCAL, limit_slot);
    
        // 2. カウンタ変数 (0 初期化)
        int counter_slot;
        if (block_node->block.args != NULL) {
            ASTNode* id_node = block_node->block.args->var_decl.identifier;
            TypeInfo* t_info = build_type_info(block_node->block.args->var_decl.type);
            if (t_info == NULL) {
                t_info = new_type_info(sym_Integer, 0);
            }
            counter_slot = add_local(c, id_node->identifier.name, t_info);
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
        emit_inst(c, OP_LESS_INT, 0);
    
        int exit_jump = emit_jump(c, OP_JUMP_IF_FALSE);
        emit_inst(c, OP_POP, 0);
    
        // 4. 本体ブロック実行
        compile_statement_node(c, block_node->block.body);

        patch_continue_jumps(c, &loop);
    
        // 5. カウンタ加算: counter = counter + 1
        emit_inst(c, OP_GET_LOCAL, counter_slot);
        emit_constant(c, make_int(1));
        emit_inst(c, OP_ADD_INT, 0);
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

void compile_break(Compiler* c, ASTNode* node)
{
    if (c->current_loop == NULL) {
        error_compile(node, ERR_BREAK_OUTSIDE_LOOP);
        
    }
    int jump = emit_jump(c, OP_JUMP);
    uint32_t prev = (c->current_loop->break_jump == -1) 
                    ? SENTINEL_JUMP
                    : (uint32_t)c->current_loop->break_jump;
    c->chunk->code[jump] = make_inst(OP_JUMP, prev);
    c->current_loop->break_jump = jump;
}

void compile_continue(Compiler* c, ASTNode* node)
{
    if (c->current_loop == NULL) {
        error_compile(node, ERR_CONTINUE_OUTSIDE_LOOP);
        
    }

    // 回数指定ループ等で continue_jump による遅延解決が必要な場合
    // (start_pos == -1 等をフラグにするか、あるいは常にジャンプチェインにする)
    int jump = emit_jump(c, OP_JUMP);
    uint32_t prev = (c->current_loop->continue_jump == -1)
                    ? SENTINEL_JUMP
                    : (uint32_t)c->current_loop->continue_jump;
    c->chunk->code[jump] = make_inst(OP_JUMP, prev);
    c->current_loop->continue_jump = jump;
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
        Symbol* arg_name = args_node->var_decl.identifier->identifier.name;
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
    block_compiler.defined_class = parent->defined_class;

    block_compiler.expected_ret_count = 0;
    if (block_node->block.rets != NULL) {
        // AST_STMT で並んでいる ret の型ツリーをフラットに集める
        collect_ret_types_from_ast(block_node->block.rets, 
                                   block_compiler.expected_ret_types, 
                                   &block_compiler.expected_ret_count);
    } else {
        // rets 省略時は Void 1個扱い
        block_compiler.expected_ret_types[0] = new_type_info(sym_Void, 0);
        block_compiler.expected_ret_count = 1;
    }

    if (parent != NULL && parent->current_class != NULL && parent->enclosing == NULL) {
        block_compiler.is_method = true;
        block_compiler.locals[0].name  = sym_self; // または sym_self
        block_compiler.locals[0].type  = parent->current_class->type; // クラスの型情報
        block_compiler.locals[0].depth = 0;
        block_compiler.local_count     = 1;
    } else {
        block_compiler.is_method = false;
        block_compiler.locals[0].name  = NULL;
        block_compiler.locals[0].type  = NULL;
        block_compiler.locals[0].depth = 0;
        block_compiler.local_count     = 1;
    }

    int arity = 0;
    if (block_node->block.args != NULL) {
        compile_block_args(&block_compiler, block_node->block.args, &arity);
    }

    compile(&block_compiler, block_node->block.body);

    Chunk* chunk = block_compiler.chunk;
    if (chunk->count == 0 || chunk->code[chunk->count - 1] != OP_RETURN) {
        if (block_compiler.is_method) {
            // メソッドなら self (スロット 0) を積んでからリターン (Smalltalk流!)
            emit_inst(&block_compiler, OP_GET_LOCAL, 0);
        } else {
            // 通常のブロックなら 0 や nil を積んでからリターン
            emit_constant(&block_compiler, make_int(0));
        }
        emit_inst(&block_compiler, OP_RETURN, 1);
    }

    ObjFunction* fn =  new_function(fn_chunk, arity);

    fn->upvalue_count = block_compiler.upvalue_count;
    if (fn->upvalue_count > 0) {
        fn->upvalues = (UpvalueInfo*)malloc(sizeof(UpvalueInfo) * fn->upvalue_count);
        for (int i = 0; i < fn->upvalue_count; i++) {
            fn->upvalues[i].index    = block_compiler.upvalues[i].index;
            fn->upvalues[i].is_local = block_compiler.upvalues[i].is_local;
        }
    } else {
        fn->upvalues = NULL;
    }

    return fn;
}

void compile_switch(Compiler* c, ASTNode* node)
{
    ASTNode* target_expr = node->send.args->stmt.left;
    ASTNode* body_block  = node->send.args->stmt.right;

    // 1. switch (target) の対象式を評価し、匿名ローカルスロットに格納
    TypeInfo* target_type =  compile_expression(c, target_expr);
    int target_slot = add_anonymous_local(c);
    emit_inst(c, OP_SET_LOCAL, (uint32_t)target_slot);
    emit_inst(c, OP_POP, 0); // スタックをクリーンにする

    // 2. 本体のブロック内にある case / default 文をフラットに収集
    ASTNode* clauses[64];
    int clause_count = 0;
    collect_clauses(body_block->block.body, clauses, &clause_count, 64);

    int exit_jumps[64];
    int exit_count = 0;

    for (int i = 0; i < clause_count; i++) {
        ASTNode* clause = clauses[i];
        if (clause == NULL || clause->kind != AST_SEND || clause->send.message == NULL) {
            continue;
        }

        Symbol* msg = clause->send.message->identifier.name;

        // --- パターン A: case (cond1, cond2, ...) [ ... ]; ---
        if (msg == sym_case) {
            ASTNode* cond_expr = clause->send.args->stmt.left;
            ASTNode* case_block = clause->send.args->stmt.right;

            ASTNode* conds[32];
            int cond_count = 0;
            collect_clauses(cond_expr, conds, &cond_count, 32);

            int match_jumps[32];
            int match_count = 0;

            for (int j = 0; j < cond_count; j++) {
                bool is_last = (j == cond_count - 1);

                // target_val == conds[j] を比較 (スタック: +1)
                emit_inst(c, OP_GET_LOCAL, (uint32_t)target_slot);
                TypeInfo* cond_t = compile_expression(c, conds[j]);

                // switch 対象型と case 条件型の照合
                if (target_type != NULL && cond_t != NULL && !type_equals(target_type, cond_t)) {
                    error_compile(conds[j], ERR_CASE_TYPE_MISMATCH,
                                  type_to_string(target_type),
                                  type_to_string(cond_t));
                }
                emit_inst(c, OP_EQUAL, 0);

                if (!is_last) {
                    // 一致しなかったら次の条件式へジャンプ
                    int not_match_jump = emit_jump(c, OP_JUMP_IF_FALSE);

                    // 一致した！（真のパス）
                    emit_inst(c, OP_POP, 0); // 真の値を pop (スタック: 0)
                    match_jumps[match_count++] = emit_jump(c, OP_JUMP); // 本体へジャンプ

                    // 不一致パスの着地点 (ジャンプ元からはスタックに 1 つ乗って飛んでくる)
                    patch_jump(c, not_match_jump);
                    c->stack_depth++;        // ★★★ スタック高さを世界線に合わせて補正！
                    emit_inst(c, OP_POP, 0); // 偽の値を pop (スタック: 0)

                } else {
                    // 最後の条件：一致しなかったら次の case へジャンプ
                    int next_case_jump = emit_jump(c, OP_JUMP_IF_FALSE);

                    // 一致した！（真のパス）
                    emit_inst(c, OP_POP, 0); // 真の値を pop (スタック: 0)

                    // 途中の条件からマッチして飛んできたジャンプを合流
                    for (int m = 0; m < match_count; m++) {
                        patch_jump(c, match_jumps[m]);
                    }

                    // 本体ブロック実行
                    compile_statement_node(c, case_block->block.body);

                    // switch 全体の末尾へ自動脱出
                    exit_jumps[exit_count++] = emit_jump(c, OP_JUMP);

                    // 最後の条件も不一致だったときの着地点 (次の case の直前)
                    patch_jump(c, next_case_jump);
                    c->stack_depth++;        // ★★★ スタック高さを世界線に合わせて補正！
                    emit_inst(c, OP_POP, 0); // 偽の値を pop (スタック: 0)
                }
            }

        // --- パターン B: default () [ ... ]; ---
        } else if (msg == sym_default) {
            ASTNode* def_block = (clause->send.args->kind == AST_BLOCK)
                                 ? clause->send.args
                                 : clause->send.args->stmt.right;

            compile_statement_node(c, def_block->block.body);

            // switch 出口へ脱出
            exit_jumps[exit_count++] = emit_jump(c, OP_JUMP);
        }
    }

    // 3. 各節の脱出ジャンプを switch 全体の末尾へパッチ
    for (int i = 0; i < exit_count; i++) {
        patch_jump(c, exit_jumps[i]);
    }
}

void compile_each(Compiler* c, ASTNode* node)
{
    if (node->send.args == NULL || node->send.args->kind != AST_BLOCK) {
        error_compile(node, ERR_WRONG_ARG_COUNT);
        return;
    }
    ASTNode* block_node = node->send.args;

    // 1. 引数宣言の収集 (1個または2個)
    ASTNode* arg_decls[4];
    int arg_count = 0;
    collect_args(block_node->block.args, arg_decls, &arg_count, 4);

    if (arg_count < 1 || arg_count > 2) {
        error_compile(block_node, ERR_EACH_ARITY, arg_count);
        return;
    }

    // 2. レシーバ（リスト）を評価してスロットに保持 (スタック: +1)
    TypeInfo* rec_type = compile_expression(c, node->send.receiver);
    if (rec_type != NULL && !is_collection_type(rec_type->name) && 
        rec_type->name != sym_Auto && rec_type->name != sym_Error) {
        error_compile(node->send.receiver, ERR_EACH_NOT_LIST, rec_type->name->chars);
        return;
    }
    int list_slot = add_anonymous_local(c);
    emit_inst(c, OP_SET_LOCAL, (uint32_t)list_slot);
    // ★ OP_POP はしない！スタックに乗せたままローカル変数として維持する

    TypeInfo* elem_type = (rec_type && rec_type->type_arg_count > 0)
                          ? rec_type->type_args[0]
                          : new_type_info(sym_Auto, 0);

    // 3. リストの長さ (length) を取得してスロットに保持 (スタック: +1)
    ClassDef* list_class = resolve_class(c, sym_List);
    int len_vtable_idx = list_class ? get_vtable_index(list_class, intern_cstr("length")) : -1;
    if (len_vtable_idx == -1) {
        error_compile(node, ERR_BUILTIN_CACHE);
        return;
    }

    emit_inst(c, OP_GET_LOCAL, (uint32_t)list_slot); // length のレシーバを積む (stack: +1)
    uint32_t len_op = ((uint32_t)0 << 16) | ((uint16_t)len_vtable_idx & 0xFFFF);
    emit_inst(c, OP_INVOKE_VIRTUAL, len_op); // レシーバを消費して length (Integer) を残す

    int len_slot = add_anonymous_local(c);
    emit_inst(c, OP_SET_LOCAL, (uint32_t)len_slot);
    // ★ OP_POP はしない！

    // 4. カウンタ変数 i = 0 (スタック: +1)
    int i_slot = add_anonymous_local(c);
    emit_constant(c, make_int(0));
    emit_inst(c, OP_SET_LOCAL, (uint32_t)i_slot);
    // ★ OP_POP はしない！

    // 5. ブロックの引数スロットを確保 & 静的型チェック
    int idx_slot = -1;
    int elem_slot = -1;

    if (arg_count == 1) {
        // [ arg n: Type; ] ➜ 要素のみ (スタック: +1)
        ASTNode* decl = arg_decls[0];
        Symbol* name = decl->var_decl.identifier->identifier.name;
        TypeInfo* declared_type = build_type_info(decl->var_decl.type);

        if (declared_type != NULL && elem_type != NULL && elem_type->name != sym_Auto) {
            if (!is_assignable(c, declared_type, elem_type)) {
                error_compile(decl, ERR_EACH_ELEMENT_TYPE,
                              type_to_string(elem_type),
                              type_to_string(declared_type));
                return;
            }
        }

        elem_slot = add_local(c, name, declared_type ? declared_type : elem_type);
        emit_constant(c, make_nil());
        emit_inst(c, OP_SET_LOCAL, (uint32_t)elem_slot);
        // ★ OP_POP はしない！

    } else {
        // [ arg i: Integer, n: Type; ] ➜ 添字と要素 (スタック: +2)
        ASTNode* decl_i = arg_decls[0];
        Symbol* name_i = decl_i->var_decl.identifier->identifier.name;
        TypeInfo* idx_type = build_type_info(decl_i->var_decl.type);

        if (idx_type != NULL && !is_integer_type(idx_type->name) && idx_type->name != sym_Auto) {
            error_compile(decl_i, ERR_EACH_INDEX_TYPE, idx_type->name->chars);
            return;
        }

        idx_slot = add_local(c, name_i, idx_type ? idx_type : new_type_info(sym_Integer, 0));
        emit_constant(c, make_int(0));
        emit_inst(c, OP_SET_LOCAL, (uint32_t)idx_slot);

        ASTNode* decl_n = arg_decls[1];
        Symbol* name_n = decl_n->var_decl.identifier->identifier.name;
        TypeInfo* declared_elem_type = build_type_info(decl_n->var_decl.type);

        if (declared_elem_type != NULL && elem_type != NULL && elem_type->name != sym_Auto) {
            if (!is_assignable(c, declared_elem_type, elem_type)) {
                error_compile(decl_n, ERR_EACH_ELEMENT_TYPE,
                              type_to_string(elem_type),
                              type_to_string(declared_elem_type));
                return;
            }
        }

        elem_slot = add_local(c, name_n, declared_elem_type ? declared_elem_type : elem_type);
        emit_constant(c, make_nil());
        emit_inst(c, OP_SET_LOCAL, (uint32_t)elem_slot);
    }

    // 6. ループ先頭
    int loop_start = c->chunk->count;
    Loop loop;
    start_loop(c, &loop, loop_start);

    // 継続条件: i < len (スタック: +1)
    emit_inst(c, OP_GET_LOCAL, (uint32_t)i_slot);   // stack: +1
    emit_inst(c, OP_GET_LOCAL, (uint32_t)len_slot); // stack: +1
    emit_inst(c, OP_LESS_INT, 0);                   // stack: -1 (真偽値が 1 個乗る)

    int exit_jump = emit_jump(c, OP_JUMP_IF_FALSE);
    emit_inst(c, OP_POP, 0); // 真のパス: 条件値を pop してループ内へ

    // 7. 要素の取り出し: list[i] ➜ elem_slot
    emit_inst(c, OP_GET_LOCAL, (uint32_t)list_slot); // stack: +1
    emit_inst(c, OP_GET_LOCAL, (uint32_t)i_slot);    // stack: +1
    emit_inst(c, OP_INDEX_GET, 0);                   // stack: -1 (要素値が 1 個残る)
    emit_inst(c, OP_SET_LOCAL, (uint32_t)elem_slot); // peek 代入
    emit_inst(c, OP_POP, 0);                         // 要素値を pop してクリーンにする

    // 2引数の場合は添字もセット: i ➜ idx_slot
    if (arg_count == 2) {
        emit_inst(c, OP_GET_LOCAL, (uint32_t)i_slot);
        emit_inst(c, OP_SET_LOCAL, (uint32_t)idx_slot);
        emit_inst(c, OP_POP, 0);
    }

    // 8. 本体ブロックの実行 (文末の POP によりブロック終了時は stack 増減 0)
    compile_statement_node(c, block_node->block.body);

    // ★ continue の合流地点（カウンタ加算へジャンプしてくる）
    patch_continue_jumps(c, &loop);

    // 9. カウンタ加算: i = i + 1
    emit_inst(c, OP_GET_LOCAL, (uint32_t)i_slot); // stack: +1
    emit_constant(c, make_int(1));                // stack: +1
    emit_inst(c, OP_ADD_INT, 0);                  // stack: -1
    emit_inst(c, OP_SET_LOCAL, (uint32_t)i_slot); // peek 代入
    emit_inst(c, OP_POP, 0);                      // 加算結果を pop

    // 10. ループ巻き戻し
    emit_loop(c, loop_start);

    // ★★★ 偽の世界線（exit_jump）からの脱出合流地点 ★★★
    patch_jump(c, exit_jump);
    c->stack_depth++;        // 偽の世界線（条件値が 1 個乗っている状態）に合わせて補正！
    emit_inst(c, OP_POP, 0); // 条件値を pop

    end_loop(c, &loop); // break はここへ脱出パッチされる
}
