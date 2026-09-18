#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"
#include "object.h"

#define SENTINEL_JUMP 0xFFFFFF

static ObjString* sym_is       = NULL;
static ObjString* sym_self     = NULL;
static ObjString* sym_if       = NULL;
static ObjString* sym_repeat   = NULL;
static ObjString* sym_SInteger = NULL;
static ObjString* sym_SFloat   = NULL;
static ObjString* sym_plus   = NULL;
static ObjString* sym_minus   = NULL;
static ObjString* sym_multi   = NULL;
static ObjString* sym_div   = NULL;
static ObjString* sym_equal   = NULL;
static ObjString* sym_less   = NULL;
static ObjString* sym_gt   = NULL;


void init_compiler_symbols(void) {
    if (!sym_is)        sym_is       = intern_cstr("is");
    if (!sym_self)      sym_self     = intern_cstr("self");
    if (!sym_if)        sym_if       = intern_cstr("if");
    if (!sym_repeat)    sym_repeat   = intern_cstr("repeat"); 
    if (!sym_SInteger)  sym_SInteger = intern_cstr("SmallInteger"); 
    if (!sym_SFloat)    sym_SFloat   = intern_cstr("SmallFloat");
    if (!sym_plus)      sym_plus     = intern_cstr("+");
    if (!sym_minus)     sym_minus    = intern_cstr("-");
    if (!sym_multi)     sym_multi    = intern_cstr("*");
    if (!sym_div)       sym_div      = intern_cstr("/");
    if (!sym_equal)     sym_equal    = intern_cstr("=");
    if (!sym_less)      sym_less     = intern_cstr("<");
    if (!sym_gt)        sym_gt       = intern_cstr(">");
}

void init_compiler(Compiler* compiler, Chunk* chunk)
{
    compiler->enclosing = NULL;
    compiler->chunk = chunk;
    compiler->local_count = 0;
    compiler->scope_depth = 0;

    compiler->locals[0].name = intern_cstr("self");
    compiler->locals[0].depth = 1;
    compiler->local_count = 1;
    compiler->current_loop = NULL;
}

static void emit_inst(Compiler* c, Opcode op, uint32_t operand)
{
    write_chunk(c->chunk, make_inst(op, operand));
    printf("[EMIT] count=%d, op=%d, operand=%u\n", c->chunk->count, op, operand);
}

static void emit_constant(Compiler* c, Value val)
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

// ----------------------------------------------------
// ループ追跡ヘルパー
// ----------------------------------------------------
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

static ObjString* get_type_name(ASTNode* type_node) {
    if (type_node == NULL) return NULL;
    if (type_node->kind == AST_IDENTIFIER) {
        return type_node->identifier.name;
    }
    if (type_node->kind == AST_CLASS && type_node->class.classname != NULL) {
        return type_node->class.classname->identifier.name;
    }
    return NULL;
}

// パーサーでの数値処理を入れる前の応急処置----------------
static bool is_integer_type(ObjString* name) {
    if (name == NULL) return false;
    return (strcmp(name->chars, "Integer") == 0 || 
            strcmp(name->chars, "SmallInteger") == 0);
}

// 即値 Float かどうかの判定 (Float / SmallFloat 両対応)
static bool is_float_type(ObjString* name) {
    if (name == NULL) return false;
    return (strcmp(name->chars, "Float") == 0 || 
            strcmp(name->chars, "SmallFloat") == 0);
}
//-----------------------------------------------------

static int resolve_local(Compiler* c, ObjString* name)
{
    for (int index = c->local_count - 1; index >= 0; index--) {
        if (c->locals[index].name == name) return index;
    }
    return -1;
}

static int add_local(Compiler* c, ObjString* name)
{
    if (c->local_count >= MAX_LOCALS) {
        fprintf(stderr, "Error: Too many local variables.\n");
        exit(EXIT_FAILURE);
    }

    int slot = c->local_count++;
    c->locals[slot].name = name;
    c->locals[slot].depth = c->scope_depth;
    return slot; 
}

static int add_anonymous_local(Compiler* c)
{
    if (c->local_count >= MAX_LOCALS) {
        fprintf(stderr, "Error: Too many local variables.\n");
        exit(EXIT_FAILURE);
    }

    int slot = c->local_count++;
    c->locals[slot].name = NULL;
    c->locals[slot].depth = c->scope_depth;
    return slot;
}

static ObjFunction* compile_block(Compiler* parent, ASTNode* block_node)
{
    Chunk* fn_chunk = (Chunk*)malloc(sizeof(Chunk));
    init_chunk(fn_chunk);

    Compiler block_compiler;
    init_compiler(&block_compiler, fn_chunk);
    block_compiler.enclosing = parent;

    int arity = 0;

    compile(&block_compiler, block_node->block.body);

    if (!block_node->block.has_ret)
        emit_inst(&block_compiler, OP_RETURN, 0);

    return new_function(fn_chunk, arity);
}

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
            ObjString* var_name  = node->var_decl.identifier->identifier.name;
            ObjString* type_name = get_type_name(node->var_decl.type);
        
            // 1. 変数スロットを確保
            add_local(c, var_name);
        
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
            
            // 生成された値がスタックトップに残り、そのままこのスロットの値となる
            break;
        }
        
        case AST_SEND: {
            ObjString* msg_sym = node->send.message->identifier.name;
            bool is_self = (node->send.receiver->kind == AST_IDENTIFIER &&
                            node->send.receiver->identifier.name == sym_self);

            // 特殊ケース: 代入
            // AST_SEND 内の代入処理
            if (msg_sym == sym_is) {
                // パターン 1: 初期化付き宣言 (x: Integer is 10.)
                if (node->send.receiver->kind == AST_VAR_DECL) {
                    ASTNode* decl = node->send.receiver;
                    ObjString* var_name  = decl->var_decl.identifier->identifier.name;
                    ObjString* type_name = get_type_name(decl->var_decl.type);
            
                    int slot = add_local(c, var_name);
            
                    // 即値型（Integer, Float 等）の場合
                    // if (type_name == sym_SInteger || type_name == sym_SFloat) {
                    if (is_float_type(type_name) || is_integer_type(type_name)) {
                        compile(c, node->send.args); // 右辺の式を評価 (スタックに乗る)
                        // スタックトップにある値がそのまま slot の領域になるため、
                        // その場で作るなら OP_SET_LOCAL すら不要（既にスタックトップにある）
                        // 明示的に書き込むなら:
                        emit_inst(c, OP_SET_LOCAL, (uint32_t)slot);
                        break;
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
                    break;
                }
            
                // パターン 2: 既存変数への再代入 (x is 20.)
                if (node->send.receiver->kind == AST_IDENTIFIER) {
                    ObjString* target_name = node->send.receiver->identifier.name;
                    int slot = resolve_local(c, target_name);
                    if (slot == -1) {
                        fprintf(stderr, "Error: Assignment to undefined variable '%s'.\n", target_name->chars);
                        exit(1);
                    }
                    compile(c, node->send.args);
                    emit_inst(c, OP_SET_LOCAL, (uint32_t)slot);
                    emit_inst(c, OP_POP, 0);
                    break;
                }
            }
            /* if */
            if (is_self && msg_sym == sym_if) {
                ASTNode* cond_node = node->send.args->stmt.left;
                ASTNode* block_node = node->send.args->stmt.right;

                compile(c, cond_node);

                int then_jump = emit_jump(c, OP_JUMP_IF_FALSE);
                emit_inst(c, OP_POP, 0);

                compile(c, block_node->block.body);

                patch_jump(c, then_jump);
                emit_inst(c, OP_POP, 0);
                break;
            }

            /* repeat */
            if (msg_sym == sym_repeat) {
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
                    break;
                }

                // パターン 2-B: repeat [ body ] (無限ループ)
                if (is_self && node->send.args->kind == AST_BLOCK) {
                    int loop_start = c->chunk->count;

                    Loop loop;
                    start_loop(c, &loop, loop_start);

                    compile(c, node->send.args->block.body);

                    emit_loop(c, loop_start);

                    end_loop(c, &loop);
                    break;
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
                        counter_slot = add_local(c, id_node->identifier.name);
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
                    break;
                }
            }
            // 通常のメッセージ送信
            compile(c, node->send.receiver);
            int argc = compile_args(c, node->send.args);

            if (argc == 1) {
                if (msg_sym == sym_plus) {emit_inst(c, OP_ADD, 0); break;}
                if (msg_sym == sym_minus) {emit_inst(c, OP_SUB, 0); break;}
                if (msg_sym == sym_multi) {emit_inst(c, OP_MUL, 0); break;}
                if (msg_sym == sym_div) {emit_inst(c, OP_DIV, 0); break;}
                if (msg_sym == sym_equal) {emit_inst(c, OP_EQUAL, 0); break;}
                if (msg_sym == sym_less) {emit_inst(c, OP_LESS, 0); break;}
                if (msg_sym == sym_gt) {emit_inst(c, OP_GREAT, 0); break;}
            }

            uint32_t sym_const = add_constant(c->chunk, make_obj((Obj*)msg_sym));
            write_chunk(c->chunk, make_send_inst((uint16_t)argc, (uint16_t)sym_const));
            break;
        }

        case AST_BREAK: {
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
            break;
        }

        case AST_CONTINUE: {
            if (c->current_loop == NULL) {
                fprintf(stderr, "Error: 'continue' outside of loop.\n");
                exit(EXIT_FAILURE);
            }
            emit_loop(c, c->current_loop->start_pos);
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
