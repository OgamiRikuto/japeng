#include "compiler_internal.h"
#include <stdio.h>
#include <stdlib.h>

// 実引数を配列にフラットに集めるヘルパー
void collect_args(ASTNode* node, ASTNode** list, int* count, int max)
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
TypeInfo* resolve_upvalue_type(Compiler* c, ObjString* name)
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
            op == sym_rem;
}

bool is_compound_operator(ObjString* op)
{
    return  op == sym_plus_eq || op == sym_minus_eq ||
            op == sym_multi_eq || op == sym_div_eq  ||
            op == sym_band_eq  || op == sym_bor_eq  ||
            op == sym_bxor_eq  || op == sym_shl_eq  || op == sym_shr_eq;
}

static bool is_bitwise_operator(ObjString* op)
{
    return op == sym_shl || op == sym_shr ||
           op == sym_band  || op == sym_bor   ||
           op == sym_bxor;
}

static bool is_raw_numeric_literal(ASTNode* node) {
    if (!node || node->kind != AST_LITERAL) return false;
    return (node->literal.kind == TOKEN_FLOAT || node->literal.kind == TOKEN_INTEGER);
}

static TypeInfo* peek_identifier_type(Compiler* c, ASTNode* node) {
    if (!node || node->kind != AST_IDENTIFIER) return NULL;

    int slot = resolve_local(c, node->identifier.name);
    if (slot != -1) return c->locals[slot].type;

    int upval = resolve_upvalue(c, node->identifier.name);
    if (upval != -1) return resolve_upvalue_type(c, node->identifier.name);

    if (c->current_class != NULL) {
        int field_idx = find_field_index(c->current_class, node->identifier.name);
        if (field_idx != -1) return c->current_class->field_infos[field_idx].type;
    }

    return NULL;
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

    // 1. auto なら無条件で代入可能
    if (target->name == sym_Auto || source->name == sym_Auto) return true;

    if (source->name == sym_Null) {
        if (!is_integer_type(target->name) &&
            !is_float_type(target->name) &&
            !is_decimal_type(target->name) &&
            !is_boolean_type(target->name)) {
            return true;
        }
        return false;
    }

    // 2. Object 型へのアップキャスト
    if (target->name == sym_Object) return true;

    // 3. ★ 高階関数の静的型照合
    if (target->name == sym_Function && source->name == sym_Function) {
        // シグネチャ指定なしの素の Function なら互いに代入可能
        if (target->type_arg_count == 0 || source->type_arg_count == 0) return true;

        // --- A. 引数型の照合 ---
        // target 側の第1引数が "..." (sym_ellipsis) の場合、引数チェックは全パス（可変長・不定）
        bool target_varargs = (target->param_count > 0 && 
                               target->type_args[0]->name == sym_ellipsis);

        if (!target_varargs) {
            // 引数の個数が一致しているか
            if (target->param_count != source->param_count) return false;

            // 各引数の型が一致しているか (source の引数が target に代入可能か)
            for (int i = 0; i < target->param_count; i++) {
                if (target->type_args[i]->name == sym_ellipsis) break; // 可変長突入
                if (!type_equals(target->type_args[i], source->type_args[i])) {
                    return false;
                }
            }
        }

        // --- B. 戻り値型の照合 ---
        int target_ret_cnt = target->type_arg_count - target->param_count;
        int source_ret_cnt = source->type_arg_count - source->param_count;

        // target 側の戻り値が "..." の場合、戻り値チェックは全パス
        bool target_ret_varargs = (target_ret_cnt > 0 && 
                                   target->type_args[target->param_count]->name == sym_ellipsis);

        if (!target_ret_varargs) {
            if (target_ret_cnt != source_ret_cnt) return false;

            for (int i = 0; i < target_ret_cnt; i++) {
                TypeInfo* t_ret = target->type_args[target->param_count + i];
                TypeInfo* s_ret = source->type_args[source->param_count + i];
                if (!is_assignable(c, t_ret, s_ret)) {
                    return false;
                }
            }
        }

        return true;
    }

    // 4. 型の完全一致
    if (type_equals(target, source)) return true;

    if (target->name == source->name && 
        target->type_arg_count > 0 && 
        target->type_arg_count == source->type_arg_count) {
        
        bool all_match = true;
        for (int i = 0; i < target->type_arg_count; i++) {
            TypeInfo* t_arg = target->type_args[i];
            TypeInfo* s_arg = source->type_args[i];

            // どちらかが Auto なら互換とする
            if (t_arg->name == sym_Auto || s_arg->name == sym_Auto) {
                continue;
            }

            if (!is_assignable(c, t_arg, s_arg)) {
                all_match = false;
                break;
            }
        }
        if (all_match) return true;
    }

    // 5. 継承関係チェック
    ObjClass* src_class = resolve_class(c, source->name);
    while (src_class != NULL && src_class->superclass != NULL) {
        if (src_class->superclass->name == target->name) return true;
        src_class = src_class->superclass;
    }

    return false;
}

static void collect_block_arg_info(Compiler* c, ASTNode* node, 
                                   TypeInfo** param_types, Value* default_vals, 
                                   int* param_count, int* min_param_count) 
{
    if (node == NULL) return;
    if (node->kind == AST_STMT) {
        collect_block_arg_info(c, node->stmt.left, param_types, default_vals, param_count, min_param_count);
        collect_block_arg_info(c, node->stmt.right, param_types, default_vals, param_count, min_param_count);
        return;
    }
    if (node->kind == AST_VAR_DECL) {
        int idx = (*param_count)++;

        TypeInfo* t = build_type_info(node->var_decl.type);
        if (t != NULL && t->name == sym_Function && node->var_decl.type->kind != AST_FUNC_TYPE) {
            ObjString* arg_name = node->var_decl.identifier ? node->var_decl.identifier->identifier.name : sym_Function;
            error_compile(node, ERR_FUNCTION_REQUIRES_SIGNATURE, arg_name->chars);
        }

        param_types[idx] = t;
        if (node->var_decl.default_val != NULL) {
            // ★ リテラル限定チェック（式やオブジェクト生成をここで弾く！）
            if (node->var_decl.default_val->kind != AST_LITERAL) {
                ObjString* name = node->var_decl.identifier ? node->var_decl.identifier->identifier.name : sym_Function;
                error_compile(node->var_decl.default_val, ERR_ARG_INIT_MUST_BE_LITERAL, name->chars);
                default_vals[idx] = make_nil();
            } else {
                TypeInfo* actual_type = NULL;
                default_vals[idx] = parse_ast_literal_with_expected(node->var_decl.default_val, t, &actual_type);
                if (!is_assignable(c, t, actual_type)) {
                    error_compile(node->var_decl.default_val, ERR_TYPE_MISMATCH,
                                  type_to_string(t), type_to_string(actual_type));
                }
            }
        } else {
            // デフォルト値なし引数
            if (*min_param_count < idx) {
                // デフォルト値あり引数の後ろにデフォルト値なし引数が配置された！
                error_compile(node, ERR_DEFAULT_ARG_ORDER);
            }
            default_vals[idx] = make_nil();
            (*min_param_count)++;
        }
    }
}

// ブロックの引数型を収集する再帰ヘルパー
// static void collect_block_arg_types(ASTNode* node, TypeInfo** list, int* count) 
// {
//     if (node == NULL) return;
//     if (node->kind == AST_STMT) {
//         collect_block_arg_types(node->stmt.left, list, count);
//         collect_block_arg_types(node->stmt.right, list, count);
//         return;
//     }
//     if (node->kind == AST_VAR_DECL) {
//         list[(*count)++] = build_type_info(node->var_decl.type);
//     }
// }

// 型情報の中の target_sym (sym_Auto) を replacement (具象型) に再帰置換する汎用関数
static TypeInfo* replace_type_param(TypeInfo* type, ObjString* target_sym, TypeInfo* replacement)
{
    if (type == NULL || replacement == NULL) return type;

    // 1. 自身が置換対象 (例: Auto 単体)
    if (type->name == target_sym) {
        return replacement;
    }

    // 2. ジェネリクス型や Function 型の場合、型引数を再帰置換
    if (type->type_arg_count > 0) {
        TypeInfo* new_t = new_type_info(type->name, type->type_arg_count);
        for (int i = 0; i < type->type_arg_count; i++) {
            new_t->type_args[i] = replace_type_param(type->type_args[i], target_sym, replacement);
        }
        new_t->param_count     = type->param_count;
        new_t->min_param_count = type->min_param_count;
        for (int i = 0; i < type->param_count; i++) {
            new_t->default_values[i] = type->default_values[i];
        }
        return new_t;
    }

    return type;
}

// 実引数（ブロック等）の戻り値から、Auto を置き換えるべき具象型を抽出する
static TypeInfo* infer_auto_from_args(TypeInfo* concrete_fn_type, ASTNode** args, int actual_argc)
{
    for (int i = 0; i < actual_argc && i < concrete_fn_type->param_count; i++) {
        TypeInfo* formal_param = concrete_fn_type->type_args[i];

        // 仮引数が Function 型で、その戻り値に Auto が指定されている場合
        if (formal_param != NULL && formal_param->name == sym_Function && 
            formal_param->type_arg_count > formal_param->param_count) {
            
            TypeInfo* formal_ret = formal_param->type_args[formal_param->param_count];
            if (formal_ret != NULL && formal_ret->name == sym_Auto) {
                // 実引数がブロックで、明示的な戻り値型 (rets) を持っている場合
                if (args[i]->kind == AST_BLOCK && args[i]->block.rets != NULL) {
                    TypeInfo* block_rets[MAX_ARGS];
                    int b_rcnt = 0;
                    collect_ret_types_from_ast(args[i]->block.rets, block_rets, &b_rcnt);
                    if (b_rcnt > 0 && block_rets[0] != NULL && 
                        block_rets[0]->name != sym_Auto && block_rets[0]->name != sym_Void) {
                        return block_rets[0]; // 呼び出し時に確定した真の型！
                    }
                }
            }
        }
    }
    return NULL;
}

// rets の AST_STMT ツリーからすべての型を配列に集めるヘルパー
void collect_ret_types_from_ast(ASTNode* node, TypeInfo** list, int* count) 
{
    if (node == NULL) return;
    if (node->kind == AST_STMT) {
        collect_ret_types_from_ast(node->stmt.left, list, count);
        collect_ret_types_from_ast(node->stmt.right, list, count);
        return;
    }
    list[(*count)++] = build_type_info(node);
}

// ブロックから詳細な Function 型 [引数1, 引数2, ..., 戻り値] を構築
TypeInfo* build_function_type_from_block(Compiler* c, ASTNode* block_node) 
{
    TypeInfo* params[16];
    Value default_vals[16];
    int param_count = 0;
    int min_param_count = 0;

    collect_block_arg_info(c, block_node->block.args, params, default_vals, &param_count, &min_param_count);

    TypeInfo* rets[16]; int r_cnt = 0;
    if (block_node->block.rets) collect_ret_types_from_ast(block_node->block.rets, rets, &r_cnt);

    TypeInfo* fn_type = new_func_type_info(param_count, params, r_cnt, rets);
    fn_type->min_param_count = min_param_count;
    for (int i = 0; i < param_count; i++) {
        fn_type->default_values[i] = default_vals[i];
    }

    return fn_type;
}

// ASTやシグネチャ内の型変数を、実型に再帰置換する (上部に配置)
TypeInfo* substitute_type(TypeInfo* formal, TypeEnv* env) 
{
    if (formal == NULL) return new_type_info(sym_Error, 0);

    // 1. 型パラメータの置換
    for (int i = 0; i < env->count; i++) {
        if (formal->name == env->params[i]) {
            return env->args[i];
        }
    }

    // 2. ジェネリクス型 / Function 型の中身を再帰置換
    if (formal->type_arg_count > 0) {
        TypeInfo* new_t = new_type_info(formal->name, formal->type_arg_count);
        for (int i = 0; i < formal->type_arg_count; i++) {
            new_t->type_args[i] = substitute_type(formal->type_args[i], env);
        }

        // ★ Function 型の場合、消えてしまっていたメタデータを復元・継承！
        if (formal->name == sym_Function) {
            new_t->param_count     = formal->param_count;
            new_t->min_param_count = formal->min_param_count;
            for (int i = 0; i < formal->param_count; i++) {
                new_t->default_values[i] = formal->default_values[i];
            }
        }

        return new_t;
    }

    // 3. 通常の具象型
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
    if (rec_type->name == sym_Void || rec_type->name == sym_Null) {
        error_compile(send_node, ERR_UNDEFINED_MESSAGE, 
                      send_node->send.message->identifier.name->chars, 
                      rec_type->name->chars);
        return new_type_info(sym_Error, 0);
    }

    if (rec_type->name == sym___multi__) {
        error_compile(send_node, ERR_UNDEFINED_MESSAGE, 
                      send_node->send.message->identifier.name->chars, 
                      "複数戻り値 (多値)");
        return new_type_info(sym_Error, 0);
    }

    ObjString* msg = send_node->send.message->identifier.name;

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

    // ★ 5. 引数の個数チェック（デフォルト引数 min_param_count 〜 param_count に対応）
    int min_argc = concrete_fn_type->min_param_count;
    int max_argc = concrete_fn_type->param_count;

    ASTNode* args[16];
    int actual_argc = 0;
    collect_args(send_node->send.args, args, &actual_argc, 16);

    // 引数個数が範囲外の場合にエラー
    if (actual_argc < min_argc || actual_argc > max_argc) {
        if (min_argc == max_argc) {
            error_compile(send_node, ERR_ARITY_MISMATCH, max_argc, actual_argc);
        } else {
            error_compile(send_node, ERR_ARITY_RANGE_MISMATCH, min_argc, max_argc, actual_argc);
        }
        return new_type_info(sym_Error, 0);
    }

    // レシーバは呼び出し元（compile_expression）ですでにスタックに積まれているので、
    // ここでは渡された実引数だけをスタックに積んで型チェックする！
    for (int i = 0; i < actual_argc; i++) {
        TypeInfo* arg_type = compile_expression(c, args[i]);
        TypeInfo* expected_arg_type = concrete_fn_type->type_args[i];

        if (expected_arg_type == NULL || !is_assignable(c, expected_arg_type, arg_type)) {
            error_compile(args[i], ERR_ARG_TYPE_MISMATCH, i + 1,
                          type_to_string(expected_arg_type),
                          type_to_string(arg_type));
        }
    }

    // ★ 6. 省略されたデフォルト引数をスタックに補完する！
    for (int i = actual_argc; i < max_argc; i++) {
        emit_constant(c, concrete_fn_type->default_values[i]);
    }

    // ★ 7. 最大引数数 (max_argc) で OP_SEND 発行！
    int vtable_idx = get_vtable_index(klass, msg);
    if (vtable_idx != -1) {
        write_chunk(c->chunk, make_virtual_call_inst((uint8_t)max_argc, (uint16_t)vtable_idx));
    } else {
        error_compile(send_node, ERR_UNDEFINED_MESSAGE, msg->chars, rec_type->name->chars);
        return new_type_info(sym_Error, 0);
    }

    // 8. 戻り値型を返す（param_count 以降が戻り値型）
    int ret_count = concrete_fn_type->type_arg_count - concrete_fn_type->param_count;
    if (ret_count <= 0) {
        return new_type_info(sym_Void, 0);
    }

    TypeInfo* ret_type = concrete_fn_type->type_args[concrete_fn_type->param_count];

    // 実引数（ブロック等）の戻り値型から Auto を具象型に推論し、汎用的に置換！
    // (ret_type が単体の Auto でも、List Auto でも、Map Key Auto でも等しく具象化される)
    TypeInfo* inferred = infer_auto_from_args(concrete_fn_type, args, actual_argc);
    if (inferred != NULL) {
        ret_type = replace_type_param(ret_type, sym_Auto, inferred);
    }

    if (ret_count == 1) {
        return ret_type;
    }

    // 多値戻り値 (__multi__)
    TypeInfo* multi_ret = new_type_info(sym___multi__, ret_count);
    for (int i = 0; i < ret_count; i++) {
        multi_ret->type_args[i] = concrete_fn_type->type_args[concrete_fn_type->param_count + i];
    }
    c->stack_depth += (ret_count - 1);
    return multi_ret;
}

static void compile_binary_operands(Compiler* c, ASTNode* node, TypeInfo** out_lhs, TypeInfo** out_rhs)
{
    ASTNode* left_node  = node->send.receiver;
    ASTNode* right_node = node->send.args;

    // パターン B: 左が生リテラル ＆ 右が Decimal 変数 (例: 0.1 + d, 1.0 = d)
    if (is_raw_numeric_literal(left_node)) {
        TypeInfo* peek_r = peek_identifier_type(c, right_node);
        if (peek_r && peek_r->name == sym_Decimal) {
            TypeInfo* expected = new_type_info(sym_Decimal, 0);
            Value val = parse_ast_literal_with_expected(left_node, expected, out_lhs);
            emit_constant(c, val);

            *out_rhs = compile_expression(c, right_node);
            return;
        }
    }

    // パターン A または通常: まず左辺を評価
    *out_lhs = compile_expression(c, left_node);

    // パターン A: 左辺が Decimal ＆ 右辺が生リテラル (例: d + 0.1, d = 1.0)
    if (*out_lhs && (*out_lhs)->name == sym_Decimal && is_raw_numeric_literal(right_node)) {
        TypeInfo* expected = new_type_info(sym_Decimal, 0);
        Value val = parse_ast_literal_with_expected(right_node, expected, out_rhs);
        emit_constant(c, val);
        return;
    }

    // 通常パターン (変数同士など)
    *out_rhs = compile_expression(c, right_node);
}

// 式をコンパイルしつつ、その式の型 (TypeInfo*) を返す
TypeInfo* compile_expression(Compiler* c, ASTNode* node) {
    if (node == NULL) return new_type_info(sym_Error, 0);

    switch (node->kind) {
        case AST_LITERAL: {
            TypeInfo* lit_type = NULL;
            Value val = parse_ast_literal_with_expected(node, NULL, &lit_type);
            emit_constant(c, val);
            return lit_type;
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
            ObjClass* klass = resolve_class(c, node->identifier.name);
            if (klass != NULL) {
                // クラスオブジェクトそのものをスタックに積む
                uint32_t const_idx = add_constant(c->chunk, make_obj((Obj*)klass));
                emit_inst(c, OP_CONSTANT, const_idx);

                // 型としてそのクラス自身を返す
                return new_type_info(klass->name, 0);
            }
            error_compile(node, ERR_UNDEFINED_VAR, node->identifier.name->chars);
            emit_constant(c, make_nil());
            return new_type_info(sym_Error, 0);
        }

        case AST_BLOCK: {
            ObjFunction* fn = compile_block(c, node);
            uint32_t fn_idx = add_constant(c->chunk, make_obj((Obj*)fn));
            emit_inst(c, (fn->upvalue_count > 0) ? OP_CLOSURE : OP_CONSTANT, fn_idx);
            return build_function_type_from_block(c, node);
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
                        bool has_ellipsis = false;
                        for (int k = 0; k < fn_type->param_count; k++) {
                            if (fn_type->type_args[k]->name == sym_ellipsis) {
                                has_ellipsis = true;
                                break;
                            }
                        }

                        int min_argc = fn_type->min_param_count;
                        int max_argc = fn_type->param_count;

                        if (!has_ellipsis && (actual_argc < min_argc || actual_argc > max_argc)) {
                            if (min_argc == max_argc) {
                                error_compile(node, ERR_ARITY_MISMATCH, max_argc, actual_argc);
                            } else {
                                error_compile(node, ERR_ARITY_RANGE_MISMATCH, min_argc, max_argc, actual_argc);
                            }
                            emit_constant(c, make_nil());
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
                                                    type_to_string(expected_arg_type),
                                                    type_to_string(arg_type));
                                    // return new_type_info(sym_Error, 0);
                                }
                            }
                        }
                    }

                    if (fn_type != NULL && fn_type->name == sym_Function) {
                        for (int i = actual_argc; i < fn_type->param_count; i++) {
                            emit_constant(c, fn_type->default_values[i]);
                        }
                    }

                    int call_argc = (fn_type && fn_type->name == sym_Function && fn_type->param_count > actual_argc)
                                    ? fn_type->param_count : actual_argc;
                    emit_inst(c, OP_CALL, (uint32_t)call_argc);

                    // 関数の戻り値型を返す（Function型の末尾が戻り値型）
                    if (fn_type != NULL && fn_type->type_arg_count > 0) {
                        int ret_count = fn_type->type_arg_count - fn_type->param_count;
                        if (ret_count == 1) {
                            return fn_type->type_args[fn_type->param_count];
                        } else if (ret_count > 1) {
                            // 多値戻り値型 (__multi__) としてパッケージ化して返す
                            TypeInfo* multi_ret = new_type_info(sym___multi__, ret_count);
                            for (int i = 0; i < ret_count; i++) {
                                multi_ret->type_args[i] = fn_type->type_args[fn_type->param_count + i];
                            }
                            // スタック上には 2 つ以上の値が残るので、スタック深さを補正
                            c->stack_depth += (ret_count - 1);
                            return multi_ret;
                        }
                    }
                    return new_type_info(sym_Auto, 0);
                }
            }

            if (msg == sym_bnot && node->send.args == NULL) {
                TypeInfo* rec_type = compile_expression(c, node->send.receiver);
                if (!is_integer_type(rec_type->name)) {
                    error_compile(node, ERR_TYPE_MISMATCH, sym_Integer->chars, rec_type->name->chars);
                    return new_type_info(sym_Error, 0);
                }
                emit_inst(c, OP_BNOT, 0);
                return rec_type;
            }

            // ★ ビット・シフト二項演算 (<<, >>, &, |, ^)
            if (is_bitwise_operator(msg)) {
                TypeInfo* lhs = NULL;
                TypeInfo* rhs = NULL;
                compile_binary_operands(c, node, &lhs, &rhs);

                if (!is_integer_type(lhs->name) || !is_integer_type(rhs->name)) {
                    error_compile(node, ERR_TYPE_MISMATCH, sym_Integer->chars, 
                                  !is_integer_type(lhs->name) ? lhs->name->chars : rhs->name->chars);
                    return new_type_info(sym_Error, 0);
                }

                if (msg == sym_shl)     emit_inst(c, OP_SHL, 0);
                else if (msg == sym_shr) emit_inst(c, OP_SHR, 0);
                else if (msg == sym_band)  emit_inst(c, OP_BAND, 0);
                else if (msg == sym_bor)   emit_inst(c, OP_BOR, 0);
                else if (msg == sym_bxor)  emit_inst(c, OP_BXOR, 0);

                return lhs; // Integer
            }
            // 短絡論理積
            if (msg == sym_and) {
                // 1. 左辺の評価
                TypeInfo* lhs_type = compile_expression(c, node->send.receiver);
                if (!is_boolean_type(lhs_type->name)) {
                    error_compile(node->send.receiver, ERR_TYPE_MISMATCH, sym_Boolean->chars, lhs_type->name->chars);
                    return new_type_info(sym_Error, 0);
                }

                // 2. 偽なら右辺を評価せず終了地点へジャンプ (スタック上の lhs はそのまま残す)
                int false_jump = emit_jump(c, OP_JUMP_IF_FALSE);

                // 3. 真のパス: スタック上の真値を捨てて、右辺を評価
                emit_inst(c, OP_POP, 0);
                TypeInfo* rhs_type = compile_expression(c, node->send.args);
                if (!is_boolean_type(rhs_type->name)) {
                    error_compile(node->send.args, ERR_TYPE_MISMATCH, sym_Boolean->chars, rhs_type->name->chars);
                    return new_type_info(sym_Error, 0);
                }

                // 4. ジャンプ先をパッチ (左辺が偽だった場合はここに着地し、左辺値がスタックトップに残る)
                patch_jump(c, false_jump);
                return new_type_info(sym_Boolean, 0);
            }

            // 短絡論理和 (or)
            if (msg == sym_or) {
                // 1. 左辺の評価
                TypeInfo* lhs_type = compile_expression(c, node->send.receiver);
                if (!is_boolean_type(lhs_type->name)) {
                    error_compile(node->send.receiver, ERR_TYPE_MISMATCH, sym_Boolean->chars, lhs_type->name->chars);
                    return new_type_info(sym_Error, 0);
                }

                // 2. 偽なら右辺を評価する場所へジャンプ
                int eval_rhs_jump = emit_jump(c, OP_JUMP_IF_FALSE);

                // 3. 真のパス: 右辺を評価せず式末尾へ脱出 (真値がスタックに残る)
                int exit_jump = emit_jump(c, OP_JUMP);

                // 4. 偽のパス: 偽値を捨てて右辺を評価
                patch_jump(c, eval_rhs_jump);
                emit_inst(c, OP_POP, 0);

                TypeInfo* rhs_type = compile_expression(c, node->send.args);
                if (!is_boolean_type(rhs_type->name)) {
                    error_compile(node->send.args, ERR_TYPE_MISMATCH, sym_Boolean->chars, rhs_type->name->chars);
                    return new_type_info(sym_Error, 0);
                }

                // 5. 式末尾へのジャンプ先をパッチ
                patch_jump(c, exit_jump);
                return new_type_info(sym_Boolean, 0);
            }
            // 単項論理否定 
            if (msg == sym_not && node->send.args == NULL) {
                TypeInfo* rec_type = compile_expression(c, node->send.receiver);
                if (!is_boolean_type(rec_type->name)) {
                    error_compile(node, ERR_TYPE_MISMATCH, sym_Boolean->chars, rec_type->name->chars);
                    return new_type_info(sym_Error, 0);
                }
                emit_inst(c, OP_NOT, 0);
                return rec_type; // Boolean
            }

            // ★ 算術二項演算 (+, -, *, /)
            if (is_calc_operator(msg)) {
                TypeInfo* lhs = NULL;
                TypeInfo* rhs = NULL;
                compile_binary_operands(c, node, &lhs, &rhs);

                // 文字列の結合 (+)
                if (is_string_type(lhs->name)) {
                    if (msg == sym_plus) {
                        if (!is_string_type(rhs->name)) {
                            error_compile(node, ERR_TYPE_MISMATCH, sym_String->chars, rhs->name->chars);
                            return new_type_info(sym_Error, 0);
                        }
                        emit_inst(c, OP_CONCAT, 0);
                        return lhs;
                    }
                    error_compile(node, ERR_NON_NUMERIC_OPERATION, msg->chars, lhs->name->chars);
                    return new_type_info(sym_Error, 0);
                }

                // ★ 同型チェック (Float変数やInteger変数はここで確実に弾かれる！)
                if (!type_equals(lhs, rhs)) {
                    error_compile(node, ERR_TYPE_MISMATCH, lhs->name->chars, rhs->name->chars);
                    return new_type_info(sym_Error, 0);
                }

                // 1-A. 整数同士
                if (is_integer_type(lhs->name)) {
                    if (msg == sym_plus)       emit_inst(c, OP_ADD_INT, 0);
                    else if (msg == sym_minus) emit_inst(c, OP_SUB_INT, 0);
                    else if (msg == sym_multi) emit_inst(c, OP_MUL_INT, 0);
                    else if (msg == sym_div)   emit_inst(c, OP_DIV_INT, 0);
                    else if (msg == sym_rem)   emit_inst(c, OP_REM_INT, 0);
                    return lhs;
                }

                // 1-B. Float 同士
                if (is_float_type(lhs->name)) {
                    if (msg == sym_rem) {
                        error_compile(node, ERR_NON_NUMERIC_OPERATION, msg->chars, lhs->name->chars);
                        return new_type_info(sym_Error, 0);
                    }
                    if (msg == sym_plus)       emit_inst(c, OP_ADD_FLOAT, 0);
                    else if (msg == sym_minus) emit_inst(c, OP_SUB_FLOAT, 0);
                    else if (msg == sym_multi) emit_inst(c, OP_MUL_FLOAT, 0);
                    else if (msg == sym_div)   emit_inst(c, OP_DIV_FLOAT, 0);
                    return lhs;
                }

                // 1-C. Decimal 同士
                if (is_decimal_type(lhs->name)) {
                    if (msg == sym_plus)       emit_inst(c, OP_ADD_DEC, 0);
                    else if (msg == sym_minus) emit_inst(c, OP_SUB_DEC, 0);
                    else if (msg == sym_multi) emit_inst(c, OP_MUL_DEC, 0);
                    else if (msg == sym_div)   emit_inst(c, OP_DIV_DEC, 0);
                    else if (msg == sym_rem) {
                        error_compile(node, ERR_NON_NUMERIC_OPERATION, msg->chars, lhs->name->chars);
                        return new_type_info(sym_Error, 0);
                    }
                    return lhs;
                }

                return resolve_method_call_type(c, node, lhs);
            }

            // ★ 比較二項演算 (==, !=, <, >)
            if (is_compare_operator(msg)) {
                TypeInfo* lhs = NULL;
                TypeInfo* rhs = NULL;
                compile_binary_operands(c, node, &lhs, &rhs);

                if (lhs == NULL || rhs == NULL || lhs->name == sym_Error || rhs->name == sym_Error) {
                    return new_type_info(sym_Error, 0);
                }

                bool is_equality = (msg == sym_equal || msg == sym_nequal);
                bool has_null = (lhs->name == sym_Null || rhs->name == sym_Null);

                if (is_equality && has_null) {
                    TypeInfo* non_null = (lhs->name == sym_Null) ? rhs : lhs;
                    // 生の数値型や Boolean と null の比較は弾く
                    if (is_integer_type(non_null->name) ||
                        is_float_type(non_null->name) ||
                        is_decimal_type(non_null->name) ||
                        is_boolean_type(non_null->name)) {
                        error_compile(node, ERR_TYPE_MISMATCH, non_null->name->chars, sym_Null->chars);
                        return new_type_info(sym_Error, 0);
                    }

                    // オブジェクト/ポインタ同士の比較命令を発行
                    if (msg == sym_equal)  emit_inst(c, OP_EQUAL, 0);
                    if (msg == sym_nequal) emit_inst(c, OP_NEQUAL, 0);
                    return new_type_info(sym_Boolean, 0);
                }


                // 同型チェック (暗黙の異型比較を弾く)
                if (!type_equals(lhs, rhs)) {
                    error_compile(node, ERR_TYPE_MISMATCH, lhs->name->chars, rhs->name->chars);
                    return new_type_info(sym_Error, 0);
                }
                // 1. 整数型同士
                if (is_integer_type(lhs->name)) {
                    if (msg == sym_equal)       emit_inst(c, OP_EQUAL_INT, 0);
                    else if (msg == sym_nequal) emit_inst(c, OP_NEQUAL_INT, 0);
                    else if (msg == sym_less)   emit_inst(c, OP_LESS_INT, 0);
                    else if (msg == sym_gt)     emit_inst(c, OP_GREAT_INT, 0);
                    else if (msg == sym_nless)  emit_inst(c, OP_NLESS_INT, 0);
                    else if (msg == sym_ngt)    emit_inst(c, OP_NGREAT_INT, 0);
                    return new_type_info(sym_Boolean, 0);
                }

                // 2. Float / Decimal / String
                if (is_float_type(lhs->name) || is_decimal_type(lhs->name) || is_string_type(lhs->name)) {
                    if (msg == sym_equal)       emit_inst(c, OP_EQUAL, 0);
                    else if (msg == sym_nequal) emit_inst(c, OP_NEQUAL, 0);
                    else if (msg == sym_less)   emit_inst(c, OP_LESS, 0);
                    else if (msg == sym_gt)     emit_inst(c, OP_GREAT, 0);
                    else if (msg == sym_nless)  emit_inst(c, OP_NLESS, 0);
                    else if (msg == sym_ngt)    emit_inst(c, OP_NGREAT, 0);
                    return new_type_info(sym_Boolean, 0);
                }

                return resolve_method_call_type(c, node, lhs);
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

static void type_to_string_rec(TypeInfo* type, char* out, size_t max_len) 
{
    if (type == NULL) {
        strncat(out, "unknown", max_len - strlen(out) - 1);
        return;
    }
    if (type->name == sym_Auto) {
        strncat(out, "Auto", max_len - strlen(out) - 1);
        return;
    }

    // 1. 高階関数の場合: Function(引数...; 戻り値...)
    if (type->name == sym_Function) {
        strncat(out, "Function(", max_len - strlen(out) - 1);

        // 引数リスト (0個なら何も出力されない)
        for (int i = 0; i < type->param_count; i++) {
            if (i > 0) strncat(out, ", ", max_len - strlen(out) - 1);
            type_to_string_rec(type->type_args[i], out, max_len);
        }

        // 必ずセミコロンを出力
        strncat(out, ";", max_len - strlen(out) - 1);

        // 戻り値リスト (0個なら何も出力されない)
        int ret_count = type->type_arg_count - type->param_count;
        if (ret_count > 0) {
            strncat(out, " ", max_len - strlen(out) - 1);
            for (int i = 0; i < ret_count; i++) {
                if (i > 0) strncat(out, ", ", max_len - strlen(out) - 1);
                type_to_string_rec(type->type_args[type->param_count + i], out, max_len);
            }
        }

        strncat(out, ")", max_len - strlen(out) - 1);
        return;
    }

    // 2. ジェネリクス・クラス型の場合 (例: List Integer)
    if (type->type_arg_count > 0) {
        strncat(out, type->name ? type->name->chars : "unknown", max_len - strlen(out) - 1);
        for (int i = 0; i < type->type_arg_count; i++) {
            strncat(out, " ", max_len - strlen(out) - 1);
            type_to_string_rec(type->type_args[i], out, max_len);
        }
        return;
    }

    // 3. 通常クラス・基本型
    strncat(out, type->name ? type->name->chars : "unknown", max_len - strlen(out) - 1);
}

char* type_to_string(TypeInfo* type) 
{
    static char bufs[4][256];
    static int buf_idx = 0;

    // 呼ぶたびに bufs[0] -> bufs[1] -> bufs[2] -> bufs[3] と切り替わる！
    char* buf = bufs[buf_idx];
    buf_idx = (buf_idx + 1) % 4;

    buf[0] = '\0';
    type_to_string_rec(type, buf, 256);
    return buf;
}
