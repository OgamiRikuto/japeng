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

// // 実引数を配列にフラットに集めるヘルパー
// static void collect_args(ASTNode* node, ASTNode** list, int* count, int max)
// {
//     if (node == NULL || *count >= max) return;
//     if (node->kind == AST_STMT) {
//         collect_args(node->stmt.left, list, count, max);
//         collect_args(node->stmt.right, list, count, max);
//         return;
//     }
//     list[(*count)++] = node;
// }

// 複合代入シンボルに対応する演算オペコードを解決
static Opcode resolve_compound_op(ObjString* msg) 
{
    if (msg == sym_plus_eq)  return OP_ADD;
    if (msg == sym_minus_eq) return OP_SUB;
    if (msg == sym_multi_eq) return OP_MUL;
    if (msg == sym_div_eq)   return OP_DIV;
    return (Opcode)-1;
}

// 複合代入のコード生成 (ローカル変数 or selfのフィールド)
bool compile_compound_assignment(Compiler* c, ASTNode* node) 
{
    ObjString* msg = node->send.message->identifier.name;
    Opcode op = resolve_compound_op(msg);
    if ((int)op == -1) {
        return false; // 複合代入ではない
    }

    ASTNode* target = node->send.receiver;
    ObjString* name = NULL;
    bool is_explicit_self = false;
    if (target->kind == AST_IDENTIFIER) name = target->identifier.name;
    else if (target->kind == AST_SEND && target->send.receiver->kind == AST_IDENTIFIER) {
        ObjString* rec_name = target->send.receiver->identifier.name;
        if (rec_name == sym_self) {
            name = target->send.message->identifier.name;
            is_explicit_self = true;
        }
    }
    if (!target || target->kind != AST_IDENTIFIER) {
        error_compile(node, ERR_COMPOUND_ASSIGN_TARGET);
        return false;
    }

    if (!is_explicit_self) {
        // パターン 1: ローカル変数の更新
        int slot = resolve_local(c, name);
        if (slot != -1) {
            emit_inst(c, OP_GET_LOCAL, (uint32_t)slot); // 1. 現在の値をロード
            compile(c, node->send.args);                 // 2. 右辺の加算値をロード
            emit_inst(c, op, 0);                         // 3. 演算実行 (OP_ADD 等)
            emit_inst(c, OP_SET_LOCAL, (uint32_t)slot);  // 4. ローカル変数に書き戻し
            
            return true;
        }
    
        // パターン 2: アップバリューの更新
        int upval_slot = resolve_upvalue(c, name);
        if (upval_slot != -1) {
            emit_inst(c, OP_GET_UPVALUE, (uint32_t)upval_slot); // 1. 現在の値をロード
            compile(c, node->send.args);                         // 2. 右辺の加算値をロード
            emit_inst(c, op, 0);                                 // 3. 演算実行 (OP_ADD 等)
            emit_inst(c, OP_SET_UPVALUE, (uint32_t)upval_slot);  // 4. アップバリューに書き戻し
            return true;
        }
    }

    // パターン 3: フィールド変数 (self) の更新
    if (c->current_class != NULL) {
        int field_idx = find_field_index(c->current_class, name);
        if (field_idx != -1) {
            // SET_FIELD 直前のスタックを [self, 新しい値] にするための積み順
            emit_load_self(c);                   // 1. SET_FIELD 用レシーバ (self)
            emit_load_self(c);                   // 2. GET_FIELD 用レシーバ (self)
            emit_inst(c, OP_GET_FIELD, (uint32_t)field_idx); // 3. 現在のフィールド値を取得
            compile(c, node->send.args);                      // 4. 右辺の加算値をロード
            emit_inst(c, op, 0);                              // 5. 演算実行
            emit_inst(c, OP_SET_FIELD, (uint32_t)field_idx);  // 6. self に書き戻し
            
            return true;
        }
    }

    error_compile(node, ERR_UNDEFINED_VAR_OR_FIELD, name->chars);
    return false;
}

void compile_var_decl(Compiler* c, ASTNode* node)
{
    ObjString* var_name  = node->var_decl.identifier->identifier.name;

    TypeInfo* t_info     = build_type_info(node->var_decl.type);
    ObjString* type_name = t_info ? t_info->name : get_type_name(node->var_decl.type);

    int slot = add_local(c, var_name, t_info);

    if (is_integer_type(type_name)) {
        emit_constant(c, make_int(0));
    } else if (is_float_type(type_name)) {
        emit_constant(c, make_float(0.0));
    } else if (is_list_type(type_name)) {
        emit_constant(c, make_obj((Obj*)new_list()));
    } else if (is_string_type(type_name)) {
        emit_constant(c, make_obj((Obj*)new_string("", 0)));
    } else {
        uint32_t class_sym_idx = add_constant(c->chunk, make_obj((Obj*)type_name));
        emit_inst(c, OP_GET_GLOBAL, class_sym_idx);
        emit_inst(c, OP_NEW_INSTANCE, 0);
    }
    emit_inst(c, OP_SET_LOCAL, (uint32_t)slot);
}

TypeInfo* compile_assignment(Compiler* c, ASTNode* node)
{
    // =========================================================
    // ケース 1: 変数宣言と初期化 (例: a: Integer is 10; / dog: Dog is "ポチ";)
    // =========================================================
    if (node->send.receiver->kind == AST_VAR_DECL) {
        ASTNode* decl = node->send.receiver;
        ObjString* var_name  = decl->var_decl.identifier->identifier.name;
        ObjString* type_name = get_type_name(decl->var_decl.type);
        TypeInfo* declared_type = build_type_info(decl->var_decl.type);

        // -----------------------------------------------------
        // 1-A. 通常の値代入 (基本型、関数ブロック、auto の場合)
        // -----------------------------------------------------
        if (is_builtin(type_name) || 
            type_name == sym_Auto || 
            type_name == sym_Function ||
            (node->send.args != NULL && node->send.args->kind == AST_BLOCK)) {

            // 1. 右辺の式を評価して実際の型を取得
            TypeInfo* actual_type = compile_expression(c, node->send.args);

            // 2. auto の型推論
            if (declared_type->name == sym_Auto) {
                declared_type = actual_type; // 右辺の型で確定！
            } else {
                // 3. 通常の代入型チェック
                if (!is_assignable(c, declared_type, actual_type)) {
                    error_compile(node, ERR_TYPE_MISMATCH,
                                  declared_type->name->chars, actual_type->name->chars);
                    return new_type_info(sym_Error, 0);
                }
            }

            // 4. 右辺がブロックならシグネチャ付き Function 型に更新
            if (node->send.args != NULL && node->send.args->kind == AST_BLOCK) {
                declared_type = build_function_type_from_block(node->send.args);
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
