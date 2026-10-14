#include "compiler_internal.h"
#include <stdio.h>
#include <stdlib.h>

// 実引数を配列にフラットに集めるヘルパー
static void collect_args(ASTNode* node, ASTNode** list, int* count, int max)
{
    if (node == NULL || *count >= max) return;
    if (node->kind == AST_STMT) {
        collect_args(node->stmt.left, list, count, max);
        collect_args(node->stmt.right, list, count, max);
        return;
    }
    list[(*count)++] = node;
}

// 外側のスコープを遡って変数の型を特定する
static TypeInfo* resolve_upvalue_type(Compiler* c, ObjString* name)
{
    if (c->enclosing == NULL) return new_type_info(sym_Error, 0);

    int local = resolve_local(c->enclosing, name);
    if (local != -1) {
        return c->enclosing->locals[local].type; // 外側で宣言された真の型！
    }
    return resolve_upvalue_type(c->enclosing, name); // さらに外側へ
}

static bool is_compare_operator(ObjString* op)
{
    return  op == sym_equal || op == sym_nequal ||
            op == sym_gt || op == sym_ngt ||
            op == sym_less || op == sym_nless;
}

static bool is_calc_operator(ObjString* op)
{
    return  op == sym_plus || op == sym_minus ||
            op == sym_multi || op == sym_div ||
            op == sym_rem || 
            op == sym_plus_eq || op == sym_minus_eq ||
            op == sym_multi_eq || op == sym_div_eq;
}

// メッセージ送信の戻り値型を解決する箇所
static TypeInfo* resolve_operator_type(TypeInfo* receiver_type, ObjString* op, TypeInfo* arg_type)
{
    if (receiver_type == NULL || op == NULL) return NULL;
    ObjString* rec_name = receiver_type->name;

    // 1. 比較演算子 (<, >, <=, >=, =, !=)
    // 数値や文字列の比較結果は、常に真偽値 (JapEng では Integer の 0 / 1)
    if (is_compare_operator(op)) return new_type_info(sym_Integer, 0);

    // 2. 数値の四則演算 (+, -, *, /, %)
    if (is_integer_type(rec_name) || is_float_type(rec_name)) {
        if (is_calc_operator(op)) return new_type_info(rec_name, 0);
    }

    // 3. 文字列の結合 (+)
    if (rec_name == sym_String && op == sym_plus) {
        return new_type_info(sym_String, 0);
    }

    return NULL; // 特殊演算子でなければ、通常のメソッド検索へ流す
}

// 2つの型が完全一致しているか判定
bool type_equals(TypeInfo* a, TypeInfo* b) {
    if (a == NULL || b == NULL) return false;
    if (a == b) return true;
    if (a->name != b->name) return false;
    if (a->type_arg_count != b->type_arg_count) return false;

    for (int i = 0; i < a->type_arg_count; i++) {
        if (!type_equals(a->type_args[i], b->type_args[i])) return false;
    }
    return true;
}

// target 型に source 型を代入できるか判定
bool is_assignable(Compiler* c, TypeInfo* target, TypeInfo* source) {
    if (target == NULL || source == NULL) return false;

    if (target->name == sym_Error || source->name == sym_Error) return true;

    // 1. auto なら無条件で代入可能（推論で確定させる） source->name == sym_Autoはまた考慮
    if (target->name == sym_Auto || source->name == sym_Auto) return true;

    // 2. Object 型へのアップキャスト
    if (target->name == sym_Object) return true;

    // 3. 高階関数の特例（シグネチャなし Function は互いに代入可能）
    if (target->name == sym_Function && source->name == sym_Function) {
        if (target->type_arg_count == 0 || source->type_arg_count == 0) return true;
    }

    // 4. 型の完全一致
    if (type_equals(target, source)) return true;

    // 5. 継承関係チェック（source が target を継承しているか）
    ObjClass* src_class = resolve_class(c, source->name);
    while (src_class != NULL && src_class->superclass != NULL) {
        if (src_class->superclass->name == target->name) return true;
        src_class = src_class->superclass;
    }

    return false;
}

// ブロックの引数型を収集する再帰ヘルパー
static void collect_block_arg_types(ASTNode* node, TypeInfo** list, int* count) 
{
    if (node == NULL) return;
    if (node->kind == AST_STMT) {
        collect_block_arg_types(node->stmt.left, list, count);
        collect_block_arg_types(node->stmt.right, list, count);
        return;
    }
    if (node->kind == AST_VAR_DECL) {
        list[(*count)++] = build_type_info(node->var_decl.type);
    }
}

// ブロックから詳細な Function 型 [引数1, 引数2, ..., 戻り値] を構築
TypeInfo* build_function_type_from_block(ASTNode* block_node) 
{
    TypeInfo* param_types[16];
    int param_count = 0;
    collect_block_arg_types(block_node->block.args, param_types, &param_count);

    TypeInfo* ret_type = (block_node->block.rets != NULL)
                         ? build_type_info(block_node->block.rets)
                         : new_type_info(sym_Void, 0);

    TypeInfo* fn_type = new_type_info(sym_Function, param_count + 1);
    for (int i = 0; i < param_count; i++) {
        fn_type->type_args[i] = param_types[i];
    }
    fn_type->type_args[param_count] = ret_type; // 末尾を戻り値型とする
    return fn_type;
}

// ASTやシグネチャ内の型変数を、実型に再帰置換する (上部に配置)
TypeInfo* substitute_type(TypeInfo* formal, TypeEnv* env) 
{
    if (formal == NULL) return new_type_info(sym_Error, 0);

    // 1. この型名自体が型パラメータ (Key や Value など) かチェック
    for (int i = 0; i < env->count; i++) {
        if (formal->name == env->params[i]) {
            return env->args[i]; // 実型 (String や Integer) に置換！
        }
    }

    // 2. もし List[Key] のようにネストしたジェネリクスなら、中身も再帰置換
    if (formal->type_arg_count > 0) {
        TypeInfo* new_t = new_type_info(formal->name, formal->type_arg_count);
        for (int i = 0; i < formal->type_arg_count; i++) {
            new_t->type_args[i] = substitute_type(formal->type_args[i], env);
        }
        return new_t;
    }

    // 3. 通常の具象型（Integer, String など）ならそのまま
    return formal;
}

// メソッド呼び出しの型解決・引数チェック・コード生成
TypeInfo* resolve_method_call_type(Compiler* c, ASTNode* send_node, TypeInfo* rec_type) 
{
    if (send_node == NULL || rec_type == NULL) return new_type_info(sym_Error, 0);

    // 1. すでにエラーが出ている場合は、連鎖エラーを出さずに黙って Error を返す
    if (rec_type->name == sym_Error) {
        return new_type_info(sym_Error, 0);
    }

    // 2. Void に対するメソッド呼び出しは専用のエラーを出す
    if (rec_type->name == sym_Void) {
        error_compile(send_node, ERR_UNDEFINED_MESSAGE, send_node->send.message->identifier.name->chars, "Void");
        return new_type_info(sym_Error, 0);
    }

    ObjString* msg = send_node->send.message->identifier.name;

    // 組み込みクラスの演算子をコンパイル
    TypeInfo* op_ret = resolve_operator_type(rec_type, msg, NULL);
    if (op_ret != NULL) {
        // 引数をコンパイルしてスタックに積む
        ASTNode* args[16];
        int actual_argc = 0;
        collect_args(send_node->send.args, args, &actual_argc, 16);
        for (int i = 0; i < actual_argc; i++) {
            compile_expression(c, args[i]);
        }

        // バイトコード命令を emit
        if (msg == sym_equal)       emit_inst(c, OP_EQUAL, 0);
        else if (msg == sym_nequal) emit_inst(c, OP_NEQUAL, 0);
        else if (msg == sym_less)   emit_inst(c, OP_LESS, 0);
        else if (msg == sym_gt)     emit_inst(c, OP_GREAT, 0);
        else if (msg == sym_plus)   emit_inst(c, OP_ADD, 0);
        else if (msg == sym_minus)  emit_inst(c, OP_SUB, 0);
        else if (msg == sym_multi)  emit_inst(c, OP_MUL, 0);
        else if (msg == sym_div)    emit_inst(c, OP_DIV, 0);

        return op_ret;
    }

    // 1. クラスの取得
    ObjClass* klass = resolve_class(c, rec_type->name);
    if (klass == NULL) {
        error_compile(send_node, ERR_CLASS_NOT_REGISTERED, rec_type->name->chars);
        return new_type_info(sym_Error, 0);
    }

    if (send_node->send.args == NULL) {
        int field_idx = find_field_index(klass, msg);
        if (field_idx != -1) {
            emit_inst(c, OP_GET_FIELD, (uint32_t)field_idx);
            return klass->field_infos[field_idx].type;
        }
    }

    // 2. メソッドのシグネチャ取得
    TypeInfo* formal_fn_type = find_method_type(klass, msg);
    if (formal_fn_type == NULL) {
        error_compile(send_node, ERR_UNDEFINED_MESSAGE, msg->chars, rec_type->name->chars);
        return new_type_info(sym_Error, 0);
    }

    // 3. 型環境のバインド (仮型引数 -> 実型引数)
    TypeEnv env;
    env.count = 0;
    if (klass->type != NULL) {
        for (int i = 0; i < klass->type->type_arg_count && i < rec_type->type_arg_count; i++) {
            env.params[env.count] = klass->type->type_args[i]->name;
            env.args[env.count]   = rec_type->type_args[i];
            env.count++;
        }
    }

    // 4. シグネチャの完全置換
    TypeInfo* concrete_fn_type = substitute_type(formal_fn_type, &env);

    // 5. 引数の個数チェック
    int expected_argc = concrete_fn_type->type_arg_count - 1;
    ASTNode* args[16];
    int actual_argc = 0;
    collect_args(send_node->send.args, args, &actual_argc, 16);

    if (actual_argc != expected_argc) {
        error_compile(send_node, ERR_ARITY_MISMATCH, expected_argc, actual_argc);
        return new_type_info(sym_Error, 0);
    }

    // ★ 注意: レシーバは呼び出し元（compile_expression）ですでにスタックに積まれているので、
    // ここでは実引数だけをスタックに積んで型チェックする！
    for (int i = 0; i < actual_argc; i++) {
        TypeInfo* arg_type = compile_expression(c, args[i]);
        TypeInfo* expected_arg_type = concrete_fn_type->type_args[i];

        // ★ expected_arg_type が NULL でないかチェックし、安全に型照合
        if (expected_arg_type == NULL || !is_assignable(c, expected_arg_type, arg_type)) {
            error_compile(args[i], ERR_ARG_TYPE_MISMATCH, i + 1,
                          expected_arg_type ? expected_arg_type->name->chars : "unknown",
                          arg_type ? arg_type->name->chars : "nil");
        }
    }

    // 6. OP_SEND 発行
    uint32_t sym_const = add_constant(c->chunk, make_obj((Obj*)msg));
    write_chunk(c->chunk, make_send_inst((uint16_t)actual_argc, (uint16_t)sym_const));

    // 置換後の真の戻り値型を返す！
    return concrete_fn_type->type_args[expected_argc];
}

// 式をコンパイルしつつ、その式の型 (TypeInfo*) を返す
TypeInfo* compile_expression(Compiler* c, ASTNode* node) {
    if (node == NULL) return new_type_info(sym_Error, 0);

    switch (node->kind) {
        case AST_LITERAL: {
            emit_constant(c, node->literal.value);
            if (is_int(node->literal.value))   return new_type_info(sym_Integer, 0);
            if (is_float(node->literal.value)) return new_type_info(sym_Float, 0);
            if (is_obj_type(node->literal.value, OBJ_STRING)) {
                return new_type_info(sym_String, 0);
            }
            if (is_nil(node->literal.value))   return new_type_info(sym_Void, 0);
            return new_type_info(sym_Error, 0);
        }

        case AST_IDENTIFIER: {
            int slot = resolve_local(c, node->identifier.name);
            if (slot != -1) {
                emit_inst(c, OP_GET_LOCAL, (uint32_t)slot);
                return c->locals[slot].type;
            }
            int upval = resolve_upvalue(c, node->identifier.name);
            if (upval != -1) {
                emit_inst(c, OP_GET_UPVALUE, (uint32_t)upval);
                return resolve_upvalue_type(c, node->identifier.name);
            }
            if (c->current_class != NULL) {
                int field_idx = find_field_index(c->current_class, node->identifier.name);
                if (field_idx != -1) {
                    emit_load_self(c);
                    emit_inst(c, OP_GET_FIELD, (uint32_t)field_idx);
                    return c->current_class->field_infos[field_idx].type;
                }
            }
            error_compile(node, ERR_UNDEFINED_VAR, node->identifier.name->chars);
            emit_constant(c, make_nil());
            return new_type_info(sym_Error, 0);
        }

        case AST_BLOCK: {
            ObjFunction* fn = compile_block(c, node);
            uint32_t fn_idx = add_constant(c->chunk, make_obj((Obj*)fn));
            emit_inst(c, (fn->upvalue_count > 0) ? OP_CLOSURE : OP_CONSTANT, fn_idx);
            return build_function_type_from_block(node);
        }

        case AST_SEND: {
            ObjString* msg = node->send.message->identifier.name;

            bool is_self = (node->send.receiver->kind == AST_IDENTIFIER &&
                            node->send.receiver->identifier.name == sym_self);

            if (is_self) {
                int var_slot = resolve_local(c, msg);
                int upval_slot = (var_slot == -1) ? resolve_upvalue(c, msg) : -1;

                if (var_slot != -1 || upval_slot != -1) {
                    TypeInfo* fn_type = (var_slot != -1) ? c->locals[var_slot].type : NULL;
                    if (fn_type == NULL && upval_slot != -1) {
                        fn_type = resolve_upvalue_type(c, msg);
                    }

                    // 引数を集めてスタックに積む
                    ASTNode* args[16];
                    int actual_argc = 0;
                    collect_args(node->send.args, args, &actual_argc, 16);

                    if (fn_type != NULL && fn_type->name == sym_Function && fn_type->type_arg_count > 0) {
                        int expected_argc = fn_type->type_arg_count - 1; // 末尾は戻り値型
                        if (actual_argc != expected_argc) {
                            error_compile(node, ERR_ARITY_MISMATCH, expected_argc, actual_argc);
                            return new_type_info(sym_Error, 0);
                        }
                    }

                    // 関数オブジェクトをスタックに積む
                    if (var_slot != -1) emit_inst(c, OP_GET_LOCAL, (uint32_t)var_slot);
                    else emit_inst(c, OP_GET_UPVALUE, (uint32_t)upval_slot);

                    // 実引数の評価
                    for (int i = 0; i < actual_argc; i++) {
                        TypeInfo* arg_type = compile_expression(c, args[i]);

                        if (fn_type != NULL && fn_type->name == sym_Function && fn_type->type_arg_count > 0) {
                            int expected_argc = fn_type->type_arg_count - 1;
                            if (i < expected_argc) {
                                TypeInfo* expected_arg_type = fn_type->type_args[i];
                                if (expected_arg_type != NULL && !is_assignable(c, expected_arg_type, arg_type)) {
                                    error_compile(args[i], ERR_ARG_TYPE_MISMATCH, i + 1,
                                                  expected_arg_type ? expected_arg_type->name->chars : "unknown",
                                                  arg_type ? arg_type->name->chars : "nil");
                                    // return new_type_info(sym_Error, 0);
                                }
                            }
                        }
                    }

                    // #call メッセージを送信！
                    uint32_t call_sym = add_constant(c->chunk, make_obj(((Obj*)sym_call)));
                    write_chunk(c->chunk, make_send_inst((uint16_t)actual_argc, (uint16_t)call_sym));

                    // 関数の戻り値型を返す（Function型の末尾が戻り値型）
                    if (fn_type != NULL && fn_type->type_arg_count > 0) {
                        return fn_type->type_args[fn_type->type_arg_count - 1];
                    }
                    return new_type_info(sym_Auto, 0);
                }
            }

            // ★ 算術二項演算 (+, -, *, /)
            if (msg == sym_plus || msg == sym_minus || msg == sym_multi || msg == sym_div) {
                TypeInfo* lhs = compile_expression(c, node->send.receiver);
                TypeInfo* rhs = compile_expression(c, node->send.args);

                if (msg == sym_plus)       emit_inst(c, OP_ADD, 0);
                else if (msg == sym_minus) emit_inst(c, OP_SUB, 0);
                else if (msg == sym_multi) emit_inst(c, OP_MUL, 0);
                else if (msg == sym_div)   emit_inst(c, OP_DIV, 0);

                if (lhs->name == sym_Float || rhs->name == sym_Float) {
                    return new_type_info(sym_Float, 0);
                }
                return new_type_info(sym_Integer, 0);
            }

            // ★ 比較二項演算 (==, !=, <, >)
            if (msg == sym_equal || msg == sym_nequal || msg == sym_less || msg == sym_gt) {
                compile_expression(c, node->send.receiver);
                compile_expression(c, node->send.args);

                if (msg == sym_equal)       emit_inst(c, OP_EQUAL, 0);
                else if (msg == sym_nequal) emit_inst(c, OP_NEQUAL, 0);
                else if (msg == sym_less)   emit_inst(c, OP_LESS, 0);
                else if (msg == sym_gt)     emit_inst(c, OP_GREAT, 0);

                return new_type_info(sym_Integer, 0);
            }

            if (msg == sym_is || msg == sym_are) {
                return compile_assignment(c, node);
            }

            // ★ インスタンスメソッド呼び出し (レシーバを push してから型解決へ)
            TypeInfo* rec_type = compile_expression(c, node->send.receiver);
            return resolve_method_call_type(c, node, rec_type);
        }

        default:
            compile(c, node);
            return new_type_info(sym_Error, 0);
    }
}
