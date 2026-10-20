#include "compiler_internal.h"
#include "opcode.h"
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

void collect_identifiers(ASTNode* node, ASTNode** list, int* count) 
{
    if (node == NULL) return;
    if (node->kind == AST_STMT) {
        collect_identifiers(node->stmt.left, list, count);
        collect_identifiers(node->stmt.right, list, count);
        return;
    }
    if (node->kind == AST_IDENTIFIER) {
        list[(*count)++] = node;
    }
}

// 複合代入の型チェックおよび演算結果型の決定
static TypeInfo* check_compound_type(Compiler* c, ASTNode* node, ObjString* msg, TypeInfo* lhs, TypeInfo* rhs) 
{
    if (lhs == NULL || rhs == NULL) return new_type_info(sym_Error, 0);
    if (lhs->name == sym_Error || rhs->name == sym_Error) return new_type_info(sym_Error, 0);

    // 1. 文字列結合 (+= のみ許可)
    if (is_string_type(lhs->name)) {
        if (msg == sym_plus_eq) {
            if (is_string_type(rhs->name)) return lhs;
            
            error_compile(node, ERR_TYPE_MISMATCH, sym_String->chars, rhs->name->chars);
            return new_type_info(sym_Error, 0);
        }
        // 文字列に対する -=, *=, /= は禁止
        error_compile(node, ERR_NON_NUMERIC_OPERATION, msg->chars, lhs->name->chars);
        return new_type_info(sym_Error, 0);
    }

    // 2. 数値型チェック
    bool lhs_num = is_integer_type(lhs->name) || is_float_type(lhs->name) || is_decimal_type(lhs->name);
    bool rhs_num = is_integer_type(rhs->name) || is_float_type(rhs->name) || is_decimal_type(rhs->name);

    if (!lhs_num) {
        error_compile(node, ERR_NON_NUMERIC_OPERATION, msg->chars, lhs->name->chars);
        return new_type_info(sym_Error, 0);
    }
    if (!rhs_num) {
        error_compile(node, ERR_NON_NUMERIC_OPERATION, msg->chars, rhs->name->chars);
        return new_type_info(sym_Error, 0);
    }

    // 3. 案A: 同型チェック (Integer += Float などの暗黙混在は弾く)
    if (!type_equals(lhs, rhs)) {
        error_compile(node, ERR_TYPE_MISMATCH, lhs->name->chars, rhs->name->chars);
        return new_type_info(sym_Error, 0);
    }

    return lhs;
}

// 最適化オペコードの決定 (整数同士なら INT 専用命令)
static Opcode resolve_actual_op(ObjString* msg, TypeInfo* lhs) 
{
    // 文字列結合 (+=)
    if (is_string_type(lhs->name)) {
        if (msg == sym_plus_eq) return OP_CONCAT;
        return (Opcode)-1;
    }

    // 整数演算
    if (is_integer_type(lhs->name)) {
        if (msg == sym_plus_eq)  return OP_ADD_INT;
        if (msg == sym_minus_eq) return OP_SUB_INT;
        if (msg == sym_multi_eq) return OP_MUL_INT;
        if (msg == sym_div_eq)   return OP_DIV_INT;
        return (Opcode)-1;
    }

    // Float 演算
    if (is_float_type(lhs->name)) {
        if (msg == sym_plus_eq)  return OP_ADD_FLOAT;
        if (msg == sym_minus_eq) return OP_SUB_FLOAT;
        if (msg == sym_multi_eq) return OP_MUL_FLOAT;
        if (msg == sym_div_eq)   return OP_DIV_FLOAT;
        return (Opcode)-1;
    }

    // Decimal 演算
    if (is_decimal_type(lhs->name)) {
        if (msg == sym_plus_eq)  return OP_ADD_DEC;
        if (msg == sym_minus_eq) return OP_SUB_DEC;
        if (msg == sym_multi_eq) return OP_MUL_DEC;
        if (msg == sym_div_eq)   return OP_DIV_DEC;
        return (Opcode)-1;
    }

    return (Opcode)-1;
}

// 複合代入のコード生成 (ローカル変数 or selfのフィールド)
bool compile_compound_assignment(Compiler* c, ASTNode* node) 
{
    ObjString* msg = node->send.message->identifier.name;

    // ★ 判定関数を呼び出す
    if (!is_compound_operator(msg)) {
        return false;
    }

    ASTNode* target = node->send.receiver;
    ObjString* name = NULL;
    bool is_explicit_self = false;

    if (target->kind == AST_IDENTIFIER) {
        name = target->identifier.name;
    } else if (target->kind == AST_SEND && target->send.receiver->kind == AST_IDENTIFIER) {
        ObjString* rec_name = target->send.receiver->identifier.name;
        if (rec_name == sym_self || rec_name == intern_cstr("自身")) {
            name = target->send.message->identifier.name;
            is_explicit_self = true;
        }
    }

    if (name == NULL) {
        error_compile(node, ERR_COMPOUND_ASSIGN_TARGET);
        return false;
    }

    // -----------------------------------------------------
    // パターン 1 & 2: ローカル変数 / アップバリュー
    // -----------------------------------------------------
    if (!is_explicit_self) {
        int slot = resolve_local(c, name);
        if (slot != -1) {
            TypeInfo* lhs_type = c->locals[slot].type;

            emit_inst(c, OP_GET_LOCAL, (uint32_t)slot);
            TypeInfo* rhs_type = compile_expression(c, node->send.args);
            check_compound_type(c, node, msg, lhs_type, rhs_type);

            Opcode actual_op = resolve_actual_op(msg, lhs_type);
            emit_inst(c, actual_op, 0);
            emit_inst(c, OP_SET_LOCAL, (uint32_t)slot);
            return true;
        }

        int upval_slot = resolve_upvalue(c, name);
        if (upval_slot != -1) {
            TypeInfo* lhs_type = resolve_upvalue_type(c, name);

            emit_inst(c, OP_GET_UPVALUE, (uint32_t)upval_slot);
            TypeInfo* rhs_type = compile_expression(c, node->send.args);
            check_compound_type(c, node, msg, lhs_type, rhs_type);

            Opcode actual_op = resolve_actual_op(msg, lhs_type);
            emit_inst(c, actual_op, 0);
            emit_inst(c, OP_SET_UPVALUE, (uint32_t)upval_slot);
            return true;
        }
    }

    // -----------------------------------------------------
    // パターン 3: フィールド変数 (self)
    // -----------------------------------------------------
    if (c->current_class != NULL) {
        int field_idx = find_field_index(c->current_class, name);
        if (field_idx != -1) {
            TypeInfo* lhs_type = c->current_class->field_infos[field_idx].type;

            emit_load_self(c);
            emit_load_self(c);
            emit_inst(c, OP_GET_FIELD, (uint32_t)field_idx);
            TypeInfo* rhs_type = compile_expression(c, node->send.args);
            check_compound_type(c, node, msg, lhs_type, rhs_type);

            Opcode actual_op = resolve_actual_op(msg, lhs_type);
            emit_inst(c, actual_op, 0);
            emit_inst(c, OP_SET_FIELD, (uint32_t)field_idx);
            return true;
        }
    }

    error_compile(node, ERR_UNDEFINED_VAR_OR_FIELD, name->chars);
    return false;
}

void compile_var_decl(Compiler* c, ASTNode* node)
{
    TypeInfo* t_info     = build_type_info(node->var_decl.type);
    ObjString* type_name = t_info ? t_info->name : get_type_name(node->var_decl.type);

    ASTNode* vars[16];
    int var_count = 0;
    collect_identifiers(node->var_decl.identifier, vars, &var_count);

    for (int i = 0; i < var_count; i++) {
        ObjString* var_name = vars[i]->identifier.name;
        int slot = add_local(c, var_name, t_info);

        if (is_integer_type(type_name)) {
            emit_constant(c, make_int(0));
        } else if (is_float_type(type_name)) {
            emit_constant(c, make_float(0.0));
        } else if (is_decimal_type(type_name)) {
            ObjDecimal* zero_dec = new_decimal_from_i64(0);
            emit_constant(c, make_obj((Obj*)zero_dec));
        } else if (is_list_type(type_name)) {
            emit_constant(c, make_obj((Obj*)new_list()));
        } else if (is_string_type(type_name)) {
            emit_constant(c, make_obj((Obj*)new_string("", 0)));
        } else if (is_boolean_type(type_name)) {
            emit_constant(c, make_bool(false));
        } else if (type_name == sym_Function || type_name == sym_Auto) {
            emit_constant(c, make_nil());
        } else {
            uint32_t class_sym_idx = add_constant(c->chunk, make_obj((Obj*)type_name));
            emit_inst(c, OP_GET_GLOBAL, class_sym_idx);
            emit_inst(c, OP_NEW_INSTANCE, 0);
        }
        emit_inst(c, OP_SET_LOCAL, (uint32_t)slot);
        // emit_inst(c, OP_POP, 0);
    }
}

TypeInfo* compile_assignment(Compiler* c, ASTNode* node)
{
    // =========================================================
    // ケース A: 複数変数宣言代入 (a, b: Integer are 1, 2; / a, b: Auto are ...)
    // =========================================================
    if (node->send.receiver->kind == AST_VAR_DECL &&
        node->send.receiver->var_decl.identifier->kind == AST_STMT) {

        ASTNode* var_tree = node->send.receiver->var_decl.identifier;
        TypeInfo* declared_type = build_type_info(node->send.receiver->var_decl.type);

        ASTNode* vars[16];
        int var_count = 0;
        collect_identifiers(var_tree, vars, &var_count);

        ASTNode* exprs[16];
        int expr_count = 0;
        collect_args(node->send.args, exprs, &expr_count, 16);

        // ---------------------------------------------------------
        // パターン 1: 式が 1 対 1 で並んでいる場合 (a, b: Type are 1, 2;)
        // ---------------------------------------------------------
        if (expr_count == var_count) {
            for (int i = 0; i < var_count; i++) {
                TypeInfo* actual_type = compile_expression(c, exprs[i]);
                TypeInfo* var_type = declared_type;

                if (declared_type->name == sym_Auto) {
                    var_type = actual_type;
                } else {
                    if (!is_assignable(c, declared_type, actual_type)) {
                        error_compile(exprs[i], ERR_TYPE_MISMATCH,
                                      type_to_string(declared_type), type_to_string(actual_type));
                    }
                    // ★追加: 右辺がブロックなら、変数の型をブロックの具象シグネチャで確定！
                    if (exprs[i]->kind == AST_BLOCK) {
                        var_type = actual_type;
                    }
                }

                int slot = add_local(c, vars[i]->identifier.name, var_type);
                emit_inst(c, OP_SET_LOCAL, (uint32_t)slot);
            }
            return declared_type;
        }

        // ---------------------------------------------------------
        // パターン 2: 【retの壁】右辺が単一の式（多値関数呼び出し）の場合
        // ---------------------------------------------------------
        if (expr_count == 1) {
            TypeInfo* rhs_type = compile_expression(c, exprs[0]);

            // 多値戻り値 (__multi__) かチェック
            if (rhs_type != NULL && rhs_type->name == intern_cstr("__multi__")) {
                int actual_ret_count = rhs_type->type_arg_count;

                // 個数チェック
                if (actual_ret_count != var_count) {
                    error_compile(node, ERR_ARITY_MISMATCH, var_count, actual_ret_count);
                    return new_type_info(sym_Error, 0);
                }

                // ★ スタックには既に関数の戻り値 [ret1, ret2] が積まれている！
                // それらをそのままローカル変数スロットとして登録＆Auto推論！
                for (int i = 0; i < var_count; i++) {
                    TypeInfo* var_type = declared_type;
                    if (declared_type->name == sym_Auto) {
                        var_type = rhs_type->type_args[i]; /* ★ Auto 推論で型確定！ */
                    } else if (!is_assignable(c, declared_type, rhs_type->type_args[i])) {
                        error_compile(vars[i], ERR_TYPE_MISMATCH,
                                      declared_type->name->chars, rhs_type->type_args[i]->name->chars);
                    }

                    add_local(c, vars[i]->identifier.name, var_type);
                    // 値はすでにスタック上にあるため、OP_SET_LOCAL や OP_POP は不要！
                }
                return declared_type;
            }

            error_compile(node, ERR_ARITY_MISMATCH, var_count, 1);
            return new_type_info(sym_Error, 0);
        }
    }

    // =========================================================
    // ケース B: 複数変数再代入 (a, b are 30, 40;)
    // =========================================================
    if (node->send.receiver->kind == AST_STMT) {
        ASTNode* vars[16];
        int var_count = 0;
        collect_identifiers(node->send.receiver, vars, &var_count);

        ASTNode* exprs[16];
        int expr_count = 0;
        collect_args(node->send.args, exprs, &expr_count, 16);

        // ---------------------------------------------------------
        // パターン 1: 式が 1 対 1 で並んでいる場合 (a, b are 30, 40;)
        // ---------------------------------------------------------
        if (expr_count == var_count) {
            for (int i = 0; i < var_count; i++) {
                TypeInfo* actual_type = compile_expression(c, exprs[i]);
                int slot = resolve_local(c, vars[i]->identifier.name);

                if (slot != -1) {
                    TypeInfo* expected = c->locals[slot].type;
                    if (!is_assignable(c, expected, actual_type)) {
                        error_compile(exprs[i], ERR_TYPE_MISMATCH,
                                      expected->name->chars, actual_type->name->chars);
                    }
                    emit_inst(c, OP_SET_LOCAL, (uint32_t)slot);
                    emit_inst(c, OP_POP, 0);
                } else {
                    error_compile(vars[i], ERR_UNDEFINED_VAR, vars[i]->identifier.name->chars);
                }
            }
            return new_type_info(sym_Void, 0);
        }

        // ---------------------------------------------------------
        // ★ パターン 2: 右辺が単一の式（多値関数呼び出し a, b are self f;）
        // ---------------------------------------------------------
        if (expr_count == 1) {
            TypeInfo* rhs_type = compile_expression(c, exprs[0]);

            if (rhs_type != NULL && rhs_type->name == intern_cstr("__multi__")) {
                int actual_ret_count = rhs_type->type_arg_count;

                if (actual_ret_count != var_count) {
                    error_compile(node, ERR_ARITY_MISMATCH, var_count, actual_ret_count);
                    return new_type_info(sym_Error, 0);
                }

                // 1. 各変数の存在と型チェック
                int slots[16];
                for (int i = 0; i < var_count; i++) {
                    slots[i] = resolve_local(c, vars[i]->identifier.name);
                    if (slots[i] == -1) {
                        error_compile(vars[i], ERR_UNDEFINED_VAR, vars[i]->identifier.name->chars);
                        continue;
                    }

                    TypeInfo* expected = c->locals[slots[i]].type;
                    if (!is_assignable(c, expected, rhs_type->type_args[i])) {
                        error_compile(vars[i], ERR_TYPE_MISMATCH,
                                      expected->name->chars, rhs_type->type_args[i]->name->chars);
                    }
                }

                // 2. スタックトップから逆順に取り出して変数スロットへ格納
                // スタックは [..., 第1戻り値, 第2戻り値] と積まれているので、
                // 末尾の変数 (var_count - 1) から順に pop していく
                for (int i = var_count - 1; i >= 0; i--) {
                    if (slots[i] != -1) {
                        emit_inst(c, OP_SET_LOCAL, (uint32_t)slots[i]);
                        emit_inst(c, OP_POP, 0);
                    }
                }
                return new_type_info(sym_Void, 0);
            }

            error_compile(node, ERR_ARITY_MISMATCH, var_count, 1);
            return new_type_info(sym_Error, 0);
        }

        error_compile(node, ERR_ARITY_MISMATCH, var_count, expr_count);
        return new_type_info(sym_Error, 0);
    }

    // =========================================================
    // ケース 1: 変数宣言と初期化 (例: a: Integer is 10; / dog: Dog is "ポチ";)
    // =========================================================
    if (node->send.receiver->kind == AST_VAR_DECL) {
        ASTNode* decl = node->send.receiver;
        ObjString* var_name  = decl->var_decl.identifier->identifier.name;
        ObjString* type_name = get_type_name(decl->var_decl.type);
        TypeInfo* declared_type = build_type_info(decl->var_decl.type);

        bool is_null_literal = (node->send.args != NULL && 
                                node->send.args->kind == AST_LITERAL && 
                                node->send.args->literal.kind == TOKEN_NULL);

        // -----------------------------------------------------
        // 1-A. 通常の値代入 (基本型、関数ブロック、auto の場合)
        // -----------------------------------------------------
        if (is_builtin(type_name) || 
            type_name == sym_Auto || 
            type_name == sym_Function ||
            is_null_literal ||
            (node->send.args != NULL && node->send.args->kind == AST_BLOCK)) {

            ObjString* msg = node->send.message->identifier.name;

            // =====================================================
            // ★ List の複数初期化 (例: arr: List Integer are 0, 1;)
            // =====================================================
            if (is_list_type(type_name) && (msg == sym_is || msg == sym_are)) {
                int slot = add_local(c, var_name, declared_type);

                // 1. 空の List インスタンスを生成してスタックに積む
                uint32_t list_sym = add_constant(c->chunk, make_obj((Obj*)sym_List));
                emit_inst(c, OP_GET_GLOBAL, list_sym);
                emit_inst(c, OP_NEW_INSTANCE, 0); // スタック: [ <List> ]

                // 2. are の右辺引数をフラットに集める
                ASTNode* elems[64];
                int elem_count = 0;
                collect_args(node->send.args, elems, &elem_count, 64);

                // 期待される要素型を取得 (List<Integer> なら Integer)
                TypeInfo* expected_elem = (declared_type->type_arg_count > 0) 
                                          ? declared_type->type_args[0] 
                                          : NULL;

                uint32_t push_sym = add_constant(c->chunk, make_obj((Obj*)intern_cstr("push")));

                // 3. 各要素を評価・型チェック・push
                for (int i = 0; i < elem_count; i++) {
                    emit_inst(c, OP_DUP, 0); // [ <List>, <List> ]

                    // 右辺要素の式を評価して実際の型を取得
                    TypeInfo* actual_elem = compile_expression(c, elems[i]); // [ <List>, <List>, elem ]

                    // ★ 要素ごとの静的型チェック！
                    if (expected_elem != NULL && !is_assignable(c, expected_elem, actual_elem)) {
                        error_compile(elems[i], ERR_TYPE_MISMATCH,
                                      expected_elem->name->chars,
                                      actual_elem ? actual_elem->name->chars : "nil");
                    }

                    // list push elem を呼び出す
                    write_chunk(c->chunk, make_send_inst(1, (uint16_t)push_sym));
                    emit_inst(c, OP_POP, 0); // push の戻り値を捨てる -> [ <List> ]
                }

                // 4. 完成したリストをローカル変数に格納
                emit_inst(c, OP_SET_LOCAL, (uint32_t)slot);
                return declared_type;
            }

            // 1. 右辺の式を評価して実際の型を取得
            TypeInfo* actual_type = NULL;

            if (node->send.args != NULL && node->send.args->kind == AST_LITERAL) {
                Value lit_val = parse_ast_literal_with_expected(node->send.args, declared_type, &actual_type);
                emit_constant(c, lit_val);
            } else {
                actual_type = compile_expression(c, node->send.args);
            }

            // 2. auto の型推論
            if (declared_type->name == sym_Auto) {
                declared_type = actual_type; // 右辺の型で確定！
            } else {
                // 3. 通常の代入型チェック
                if (!is_assignable(c, declared_type, actual_type)) {
                    error_compile(node->send.receiver, ERR_TYPE_MISMATCH,
                                  type_to_string(declared_type), type_to_string(actual_type));
                    return new_type_info(sym_Error, 0);
                }
            }

            // 4. 右辺がブロックならシグネチャ付き Function 型に更新
            if (node->send.args != NULL && node->send.args->kind == AST_BLOCK) {
                declared_type = actual_type;
            }

            // 5. ローカル変数に登録してスタックの値を格納
            int slot = add_local(c, var_name, declared_type);
            emit_inst(c, OP_SET_LOCAL, (uint32_t)slot);
            return declared_type;
        }

        // -----------------------------------------------------
        // 1-B. ユーザー定義クラスのインスタンス生成 (例: dog: Dog is "ポチ";)
        // -----------------------------------------------------
        int slot = add_local(c, var_name, declared_type);

        // 1. クラスオブジェクトを取得してインスタンス生成
        uint32_t class_sym_idx = add_constant(c->chunk, make_obj((Obj*)type_name));
        emit_inst(c, OP_GET_GLOBAL, class_sym_idx);
        emit_inst(c, OP_NEW_INSTANCE, 0);

        // 2. 引数があれば DUP して init メソッドを呼び出す
        if (node->send.args != NULL) {
            emit_inst(c, OP_DUP, 0);
            int argc = compile_args(c, node->send.args);
            uint32_t init_sym = add_constant(c->chunk, make_obj((Obj*)intern_cstr("init")));
            write_chunk(c->chunk, make_send_inst((uint16_t)argc, (uint16_t)init_sym));
            emit_inst(c, OP_POP, 0); // init の戻り値を POP
        }

        // 3. 生成したインスタンスを変数に格納
        emit_inst(c, OP_SET_LOCAL, (uint32_t)slot);
        return declared_type;
    }

    // =========================================================
    // ケース 2: フィールドへの代入 (例: self.x is 10;)
    // =========================================================
    if (node->send.receiver->kind == AST_SEND) {
        ASTNode* lhs_send = node->send.receiver;
        if (lhs_send->send.receiver->kind == AST_IDENTIFIER) {
            ObjString* rec_name = lhs_send->send.receiver->identifier.name;
            if (rec_name == sym_self || rec_name == intern_cstr("自身")) {
                ObjString* field_name = lhs_send->send.message->identifier.name;
                if (c->current_class != NULL) {
                    int field_idx = find_field_index(c->current_class, field_name);
                    if (field_idx != -1) {
                        FieldInfo f_info = c->current_class->field_infos[field_idx];

                        emit_load_self(c); // 1. self を積む

                        // 2. 右辺を評価して型チェック
                        TypeInfo* val_type = compile_expression(c, node->send.args);
                        if (!is_assignable(c, f_info.type, val_type)) {
                            error_compile(node, ERR_TYPE_MISMATCH,
                                          f_info.type->name->chars, val_type->name->chars);
                            return new_type_info(sym_Error, 0);
                        }

                        emit_inst(c, OP_SET_FIELD, (uint32_t)field_idx); // 3. 代入
                        // emit_inst(c, OP_POP, 0);
                        return f_info.type;
                    }
                }
            }
        }
    }

    // =========================================================
    // ケース 3: 既存変数への再代入 (例: a is 20;)
    // =========================================================
    if (node->send.receiver->kind == AST_IDENTIFIER) {
        ObjString* target_name = node->send.receiver->identifier.name;

        // 3-A. ローカル変数の場合
        int slot = resolve_local(c, target_name);
        if (slot != -1) {
            TypeInfo* var_type = c->locals[slot].type;
            TypeInfo* val_type = compile_expression(c, node->send.args);

            if (!is_assignable(c, var_type, val_type)) {
                error_compile(node, ERR_TYPE_MISMATCH,
                              var_type->name->chars, val_type->name->chars);
                
            }

            emit_inst(c, OP_SET_LOCAL, (uint32_t)slot);
            // emit_inst(c, OP_POP, 0);
            return val_type;
        }

        // 3-B. アップバリューの場合
        int upval = resolve_upvalue(c, target_name);
        if (upval != -1) {
            TypeInfo* upval_type = compile_expression(c, node->send.args);
            emit_inst(c, OP_SET_UPVALUE, (uint32_t)upval);
            // emit_inst(c, OP_POP, 0);
            return upval_type;
        }

        // 3-C. self 省略のフィールドの場合
        if (c->current_class != NULL) {
            int field_idx = find_field_index(c->current_class, target_name);
            if (field_idx != -1) {
                emit_inst(c, OP_GET_LOCAL, 0);
                TypeInfo* field_type = compile_expression(c, node->send.args);
                emit_inst(c, OP_SET_FIELD, (uint32_t)field_idx);
                // emit_inst(c, OP_POP, 0);
                return field_type;
            }
        }

        error_compile(node, ERR_ASSIGN_UNDEFINED_VAR, target_name->chars);
        return new_type_info(sym_Error, 0);
    }
    return new_type_info(sym_Error, 0);
}

void compile_mesage_send(Compiler* c, ASTNode* node, ObjString* msg)
{
    bool is_super = (node->send.receiver->kind == AST_IDENTIFIER &&
                    (node->send.receiver->identifier.name == sym_super ||
                     node->send.receiver->identifier.name == intern_cstr("親") ||
                     node->send.receiver->identifier.name == intern_cstr("基底")));

    if (is_super) {
        if (c->current_class == NULL || c->current_class->superclass == NULL) {
            error_compile(node, ERR_UNDEFINED_VAR, "super (親クラスが存在しません)");
            return;
        }

        ObjClass* superclass = c->current_class->superclass;
        Value method_val;

        // ★ 2. コンパイル時に親クラスからメソッドを特定！
        if (!find_method(superclass, msg, &method_val)) {
            error_compile(node, ERR_UNDEFINED_MESSAGE, msg->chars);
            return;
        }

        // ★ 3. レシーバとして「現在の self (子インスタンス)」を積む
        emit_load_self(c);

        // ★ 4. 引数をスタックに積む
        int argc = compile_args(c, node->send.args);

        // ★ 5. 見つかった関数を定数テーブルに置き、直接呼び出し命令を吐く！
        uint32_t fn_const = add_constant(c->chunk, method_val);
        write_chunk(c->chunk, make_super_call_inst((uint8_t)argc, (uint16_t)fn_const));
        return;
    }

    compile_expression(c, node);

    // bool is_self = (node->send.receiver->kind == AST_IDENTIFIER &&
    //                 node->send.receiver->identifier.name == sym_self);

    // if (is_self && node->send.args == NULL && c->current_class != NULL) {
    //     int field_idx = find_field_index(c->current_class, msg);
    //     if (field_idx != -1) {
    //         emit_load_self(c);
    //         emit_inst(c, OP_GET_FIELD, (uint32_t)field_idx);
    //         return;
    //     }
    // }

    // if (is_self) {
    //     int var_slot = resolve_local(c, msg);
    //     int upval_slot = (var_slot == -1) ? resolve_upvalue(c, msg) : -1;

    //     if (var_slot != -1 || upval_slot != -1) {
    //         TypeInfo* fn_type = (var_slot != -1) ? c->locals[var_slot].type : NULL;

    //         // ★ 1. 実引数を配列に集める（最大16個）
    //         ASTNode* args[16];
    //         int actual_argc = 0;
    //         collect_args(node->send.args, args, &actual_argc, 16);

    //         // ★ 通常関数（シグネチャあり）の場合
    //         if (fn_type != NULL && fn_type->name == sym_Function && fn_type->type_arg_count > 0) {
    //             int expected_argc = fn_type->type_arg_count - 1; // 末尾は戻り値型

    //             // 引数の個数チェック
    //             if (actual_argc != expected_argc) {
    //                 error_compile(node, ERR_ARITY_MISMATCH, expected_argc, actual_argc);
    //                 exit(EXIT_FAILURE);
    //             }

    //             // レシーバ（関数オブジェクト）を積む
    //             emit_inst(c, OP_GET_LOCAL, (uint32_t)var_slot);

    //             // ★ 2. 配列から 1 つずつ取り出して評価＆型チェック！
    //             for (int i = 0; i < actual_argc; i++) {
    //                 TypeInfo* arg_type = compile_expression(c, args[i]);
    //                 if (!is_assignable(c, fn_type->type_args[i], arg_type)) {
    //                     error_compile(args[i], ERR_ARG_TYPE_MISMATCH, i + 1,
    //                                   fn_type->type_args[i]->name->chars, 
    //                                   arg_type ? arg_type->name->chars : "nil");
    //                     exit(EXIT_FAILURE);
    //                 }
    //             }
    //         } else {
    //             // 高階関数（シグネチャなし）は従来通り
    //             if (var_slot != -1) emit_inst(c, OP_GET_LOCAL, (uint32_t)var_slot);
    //             else                emit_inst(c, OP_GET_UPVALUE, (uint32_t)upval_slot);
    //             compile_args(c, node->send.args);
    //         }

    //         uint32_t call_sym = add_constant(c->chunk, make_obj((Obj*)intern_cstr("call")));
    //         write_chunk(c->chunk, make_send_inst((uint16_t)actual_argc, (uint16_t)call_sym));
    //         return;
    //     }
    // }

    // TypeInfo* rec_type = compile_expression(c, node->send.receiver);
    // if (rec_type != NULL && rec_type->name != sym_Object && rec_type->name != sym_Error) {
    //     if (resolve_method_call_type(c, node, rec_type) != NULL) {
    //         return;
    //     }
    // }
  
    // compile(c, node->send.receiver);
    // int argc = compile_args(c, node->send.args);

    // if (argc == 1) {
    //     if (msg == sym_plus) {emit_inst(c, OP_ADD, 0); return;}
    //     if (msg == sym_minus) {emit_inst(c, OP_SUB, 0); return;}
    //     if (msg == sym_multi) {emit_inst(c, OP_MUL, 0); return;}
    //     if (msg == sym_div) {emit_inst(c, OP_DIV, 0); return;}
    //     if (msg == sym_equal) {emit_inst(c, OP_EQUAL, 0); return;}
    //     if (msg == sym_nequal) {emit_inst(c, OP_NEQUAL, 0); return;}
    //     if (msg == sym_less) {emit_inst(c, OP_LESS, 0); return;}
    //     if (msg == sym_gt) {emit_inst(c, OP_GREAT, 0); return;}
    // }

    // uint32_t sym_const = add_constant(c->chunk, make_obj((Obj*)msg));
    // write_chunk(c->chunk, make_send_inst((uint16_t)argc, (uint16_t)sym_const));
}
