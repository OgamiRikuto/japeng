#include "compiler_internal.h"
#include "parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

static Symbol* get_class_name_from_node(ASTNode* node)
{
    if (node == NULL) return NULL;
    if (node->kind == AST_IDENTIFIER) return node->identifier.name;
    if (node->kind == AST_CLASS) return node->class.classname->identifier.name;
    return NULL;
}

static ASTNode* find_class_in_node(ASTNode* node, Symbol* name)
{
    if (node == NULL) return NULL;
    if (node->kind == AST_STMT) {
        ASTNode* found = find_class_in_node(node->stmt.left, name);
        if (found != NULL) return found;
        return find_class_in_node(node->stmt.right, name);
    }
    if (node->kind == AST_CLASS_DEF) {
        ASTNode* class_node = node->class_def.class;
        Symbol* class_name = class_node->class.classname->identifier.name;
        if (class_name == name) return node;
    }
    return NULL;
}

static ASTNode* find_class_ast(Symbol* name)
{
    if (name == NULL) return NULL;
    for (int index = 0; index < parsed_cd_count; index++) {
        ASTNode* found = find_class_in_node(parsed_cd[index], name);
        if (found != NULL) return found;
    }
    return NULL;
}

ClassDef* resolve_class(Compiler* c, Symbol* name)
{
    if (name == NULL) return NULL;

    // 1. メモリ上のテーブル確認（登録済みなら即座に返す）
    for (Compiler* comp = c; comp != NULL; comp = comp->enclosing) {
        if (comp->defined_class != NULL) {
            Value val;
            if (table_get(comp->defined_class, name, &val)) {
                return (ClassDef*)as_obj(val);
            }
        }
    }

    // ★ 修正 1: すでにパース済みの AST にある場合
    ASTNode* existing_ast = find_class_ast(name);
    if (existing_ast != NULL) {
        // ★★★ 先に骨格を登録！これで再帰時に 1 の table_get で即座に脱出できる！ ★★★
        declare_class_skeleton(c, existing_ast);
        resolve_class_skeleton_relations(c, existing_ast);
        declare_class_signatures(c, existing_ast);
        compile_class_body(c, existing_ast);

        // defined_class から取り出して返却
        for (Compiler* comp = c; comp != NULL; comp = comp->enclosing) {
            if (comp->defined_class != NULL) {
                Value val;
                if (table_get(comp->defined_class, name, &val)) return (ClassDef*)as_obj(val);
            }
        }
    }

    const char* base_dir = c->output_dir ? c->output_dir : ".";
    char bcd_path[1024];
    get_cache_filepath(base_dir, name->chars, "bcd", bcd_path, sizeof(bcd_path));

    // 2. キャッシュの存在 ＆ 鮮度チェック
    if (is_cache_fresh(bcd_path, base_dir)) {
        ClassDef* loaded = load_class_type_from_bcd(c, bcd_path);
        if (loaded) return loaded;
    }

    // B: ★ システム／組み込みライブラリのキャッシュ (lib/.je_cache/<Name>.bcd)
    const char* stdlib_dir = get_japeng_lib_dir();
    char builtin_bcd[1024];

    // インストール済みなら /usr/local/lib/japeng/<Name>.bcd を直接見に行く
    // 開発中なら ./lib/.je_cache/<Name>.bcd を見に行く
    struct stat st_lib;
    if (strstr(stdlib_dir, "usr") != NULL) {
        // インストール先 (/usr/local/lib/japeng/Object.bcd)
        encode_cache_name(name->chars, builtin_bcd, sizeof(builtin_bcd));
        char full_builtin[1024];
        snprintf(full_builtin, sizeof(full_builtin), "%s/%s.bcd", stdlib_dir, builtin_bcd);
        if (stat(full_builtin, &st_lib) == 0) {
            ClassDef* loaded = load_class_type_from_bcd(c, full_builtin);
            if (loaded) return loaded;
        }
    } else {
        // 開発環境 (./lib/.je_cache/Object.bcd)
        get_cache_filepath(stdlib_dir, name->chars, "bcd", builtin_bcd, sizeof(builtin_bcd));
        if (is_cache_fresh(builtin_bcd, stdlib_dir)) {
            ClassDef* loaded = load_class_type_from_bcd(c, builtin_bcd);
            if (loaded) return loaded;
        }
    }

    // 3. ディスク探索
    char cd_path[1024];
    bool found_source = false;

    snprintf(cd_path, sizeof(cd_path), "%s/%s.cd", base_dir, name->chars);
    struct stat st;
    if (stat(cd_path, &st) == 0) {
        found_source = true;
    } else {
        snprintf(cd_path, sizeof(cd_path), "%s/%s.jcd", base_dir, name->chars);
        if (stat(cd_path, &st) == 0) {
            found_source = true;
        }
    }

    if (found_source && parse_file(cd_path)) {
        ASTNode* ast = find_class_ast(name);
        if (ast) {
            declare_class_skeleton(c, ast);
            resolve_class_skeleton_relations(c, ast);
            declare_class_signatures(c, ast);
            compile_class_body(c, ast);

            const char* filename_only = strrchr(cd_path, '/');
            filename_only = filename_only ? filename_only + 1 : cd_path;
            write_class_bcd(base_dir, resolve_class(c, name), filename_only);

            return resolve_class(c, name);
        }
    }

    return NULL;
}

static int count_classes(ASTNode* node)
{
    if (node == NULL) return 0;
    if (node->kind == AST_STMT) {
        return count_classes(node->stmt.left) + count_classes(node->stmt.right);
    }
    return 1;
}

static void collect_delegate_classes(Compiler* c, ASTNode* node, ClassDef** list, int* idx)
{
    if (node == NULL) return;
    if (node->kind == AST_STMT) {
        collect_delegate_classes(c, node->stmt.left, list, idx);
        collect_delegate_classes(c, node->stmt.right, list, idx);
        return;
    }
    Symbol* name = get_class_name_from_node(node);
    ClassDef* klass = resolve_class(c, name);
    if (klass != NULL) {
        list[(*idx)++] = klass;
    }
}

static void collect_delegate_types(ASTNode* node, TypeInfo** list, int* idx)
{
    if (node == NULL) return;
    if (node->kind == AST_STMT) {
        collect_delegate_types(node->stmt.left, list, idx);
        collect_delegate_types(node->stmt.right, list, idx);
        return;
    }
    list[(*idx)++] = build_type_info(node);
}

int count_type_args(ASTNode* node)
{
    if (node == NULL) return 0;
    if (node->kind == AST_STMT) {
        return count_type_args(node->stmt.left) +
               count_type_args(node->stmt.right);
    }
    return 1;
}

static void fill_type_args(ASTNode* node, TypeInfo** args, int* index)
{
    if (node == NULL) return;
    if (node->kind == AST_STMT) {
        fill_type_args(node->stmt.left, args, index);
        fill_type_args(node->stmt.right, args, index);
        return;
    }
    args[(*index)++] = build_type_info(node);
}

TypeInfo* build_type_info(ASTNode* node)
{
    if (node == NULL) return NULL;

    if (node->kind == AST_CLASS) {
        Symbol* name = node->class.classname->identifier.name;
        int arg_count = count_type_args(node->class.types);

        TypeInfo* t_info = new_type_info(name, arg_count);
        if (arg_count > 0) {
            int idx = 0;
            fill_type_args(node->class.types, t_info->type_args, &idx);
        }
        return t_info;
    }

    if (node->kind == AST_FUNC_TYPE) {
        TypeInfo* params[16]; int p_cnt = 0;
        TypeInfo* rets[16];   int r_cnt = 0;
        if (node->func_type.params) collect_ret_types_from_ast(node->func_type.params, params, &p_cnt);
        if (node->func_type.rets)   collect_ret_types_from_ast(node->func_type.rets, rets, &r_cnt);

        return new_func_type_info(p_cnt, params, r_cnt, rets); // ★ 一撃で安全に生成！
    }

    if (node->kind == AST_IDENTIFIER) {
        return new_type_info(node->identifier.name, 0);
    }

    return NULL;
}

static Value get_implicit_default_value(Symbol* type_name)
{
    if (is_integer_type(type_name)) return make_int(0);
    if (is_float_type(type_name)) return make_float(0.0);
    if (is_decimal_type(type_name)) return make_obj((Obj*)new_decimal_from_i64(0));
    if (is_boolean_type(type_name)) return make_bool(false);
    return make_nil();
}

// 委譲メソッドの呼び出しを内部インスタンスへ転送する極小のラッパー関数を自動合成
static ObjFunction* create_forwarding_wrapper(Compiler* c, ClassDef* klass, ClassDef* from_class, 
                                              Symbol* member_name, TypeInfo* method_type, ASTNode* node)
{
    Symbol* hidden_name = get_delegate_field_name(from_class);
    int del_field_idx = find_field_index(klass, hidden_name);
    if (del_field_idx == -1) {
        error_compile(node, ERR_DELEGATE_CLASS_NOT_FOUND,
                      from_class->name ? from_class->name->chars : "null",
                      member_name->chars);
        return NULL;
    }

    // 引数の個数 (arity) を特定
    uint8_t arity = 0;
    if (method_type != NULL) {
        arity = (uint8_t)method_type->param_count;
    } else {
        Value val;
        if (find_method(from_class, member_name, &val) && is_obj_type(val, OBJ_FUNCTION)) {
            arity = ((ObjFunction*)as_obj(val))->arity;
        }
    }

    Chunk* chunk = (Chunk*)malloc(sizeof(Chunk));
    init_chunk(chunk);

    // 1. self (スロット 0) をロード
    write_chunk(chunk, make_inst(OP_GET_LOCAL, 0));

    // 2. 隠しフィールドから内部インスタンスを取り出す
    write_chunk(chunk, make_inst(OP_GET_FIELD, (uint32_t)del_field_idx));

    // 3. 引数 (スロット 1 〜 arity) を順番に積む
    for (uint8_t i = 1; i <= arity; i++) {
        write_chunk(chunk, make_inst(OP_GET_LOCAL, (uint32_t)i));
    }

    // 4. 内部インスタンスに対してメッセージを送信
    int vtable_idx = get_vtable_index(from_class, member_name);
    write_chunk(chunk, make_virtual_call_inst(arity, (uint16_t)vtable_idx));


    // 5. 内部インスタンスからの戻り値をそのまま返却
    int ret_count = 1;
    if (method_type != NULL && method_type->type_arg_count > method_type->param_count) {
        ret_count = method_type->type_arg_count - method_type->param_count;
    }
    write_chunk(chunk, make_inst(OP_RETURN, (uint32_t)ret_count));

    ObjFunction* wrapper = new_function(chunk, arity);
    wrapper->upvalue_count = 0;
    wrapper->upvalues = NULL;
    return wrapper;
}
// デフォルトの init メソッド（引数なし、super.init 呼び出し）を自動合成
static ObjFunction* create_default_init_function(Compiler* c, ClassDef* klass)
{
    Chunk* chunk = (Chunk*)malloc(sizeof(Chunk));
    init_chunk(chunk);

    if (klass->superclass != NULL) {
        Value super_init_val;
        // 親クラスに init があれば super 経由で呼び出す
        if (find_method(klass->superclass, sym_init, &super_init_val)) {
            // 1. self を積む
            write_chunk(chunk, make_inst(OP_GET_LOCAL, 0));
            // 2. 親の init を直接スーパーコール (引数 0)
            uint32_t fn_const = add_constant(chunk, super_init_val);
            write_chunk(chunk, make_super_call_inst(0, (uint16_t)fn_const));
            // 戻り値を捨てる
            write_chunk(chunk, make_inst(OP_POP, 0));
        }
    }

    // 3. self を積んでリターン (Smalltalk / JapEng の初期化流儀)
    write_chunk(chunk, make_inst(OP_GET_LOCAL, 0));
    write_chunk(chunk, make_inst(OP_RETURN, 1));

    ObjFunction* fn = new_function(chunk, 0); // arity: 0
    fn->upvalue_count = 0;
    fn->upvalues = NULL;
    return fn;
}

// デフォルトの destroy メソッド（引数なし、super.destroy 呼び出し）を自動合成
static ObjFunction* create_default_destroy_function(Compiler* c, ClassDef* klass)
{
    Chunk* chunk = (Chunk*)malloc(sizeof(Chunk));
    init_chunk(chunk);

    if (klass->superclass != NULL) {
        Value super_destroy_val;
        if (find_method(klass->superclass, sym_destroy, &super_destroy_val)) {
            // 1. self を積む
            write_chunk(chunk, make_inst(OP_GET_LOCAL, 0));
            // 2. 親の destroy をスーパーコール (引数 0)
            uint32_t fn_const = add_constant(chunk, super_destroy_val);
            write_chunk(chunk, make_super_call_inst(0, (uint16_t)fn_const));
            write_chunk(chunk, make_inst(OP_POP, 0));
        }
    }

    // destroy の戻り値は Void（0 個）
    write_chunk(chunk, make_inst(OP_RETURN, 0));

    ObjFunction* fn = new_function(chunk, 0); // arity: 0
    fn->upvalue_count = 0;
    fn->upvalues = NULL;
    return fn;
}

static void reserve_class_vtable_slots(ClassDef* klass, ASTNode* node)
{
    if (node == NULL) return;
    if (node->kind == AST_STMT) {
        reserve_class_vtable_slots(klass, node->stmt.left);
        reserve_class_vtable_slots(klass, node->stmt.right);
        return;
    }

    // パターン A: send 形式 (name: Function is [...];)
    if (node->kind == AST_SEND) {
        ASTNode* receiver = node->send.receiver;
        ASTNode* args = node->send.args;
        Symbol* target_name = NULL;
        if (receiver->kind == AST_IDENTIFIER) target_name = receiver->identifier.name;
        else if (receiver->kind == AST_FIELD_DECL || receiver->kind == AST_VAR_DECL) {
            target_name = receiver->var_decl.identifier->identifier.name;
        }

        if (target_name && args && args->kind == AST_BLOCK) {
            add_or_override_vtable_slot(klass, target_name, make_nil());
        }
        return;
    }

    // パターン B: field_decl 形式 (name: Function is [...]; または name: Function from X;)
    if (node->kind == AST_FIELD_DECL) {
        Symbol* member_name = node->field_decl.identifier->identifier.name;
        TypeInfo*  member_type = build_type_info(node->field_decl.type);
        if (member_type && member_type->name == sym_Function) {
            if (node->field_decl.default_data && node->field_decl.default_data->kind == AST_SEND) {
                ASTNode* send_node = node->field_decl.default_data;
                if (send_node->send.args && send_node->send.args->kind == AST_BLOCK) {
                    add_or_override_vtable_slot(klass, member_name, make_nil());
                }
            } else if (node->field_decl.from_class != NULL) {
                add_or_override_vtable_slot(klass, member_name, make_nil());
            }
        }
    }
}

static void compile_class_members(Compiler* c, ClassDef* klass, ASTNode* node)
{
    if (node == NULL) return;

    if (node->kind == AST_STMT) {
        compile_class_members(c, klass, node->stmt.left);
        compile_class_members(c, klass, node->stmt.right);
        return;
    }

    if (node->kind == AST_FIELD_DECL) {
        Symbol* member_name = node->field_decl.identifier->identifier.name;
        TypeInfo*  member_type = build_type_info(node->field_decl.type);
        ASTNode*   from_node   = node->field_decl.from_class;
        bool       is_static   = node->field_decl.is_static;

        if (member_type != NULL && member_type->name == sym_Function &&
            node->field_decl.type->kind != AST_FUNC_TYPE &&
            node->field_decl.default_data == NULL &&
            from_node == NULL) {
            
            error_compile(node, ERR_FUNCTION_REQUIRES_SIGNATURE, member_name->chars);
        }

        ClassDef* from_class = NULL;
        if (from_node != NULL) {
            Symbol* from_name = get_class_name_from_node(from_node);
            from_class  = resolve_class(c, from_name);
            if (from_class == NULL) {
                error_compile(node, ERR_DELEGATE_CLASS_NOT_FOUND,
                        from_name ? from_name->chars : "null", member_name->chars );
                
            }

            if (from_class->state != CLASS_STATE_COMPILED) {
                ASTNode* from_ast = find_class_ast(from_name);
                if (from_ast != NULL) {
                    compile_class_body(c, from_ast);
                }
            }
        }

        bool is_function = (member_type != NULL && member_type->name != NULL &&
                            member_type->name == sym_Function);
        
        if (is_function) {
            if (node->field_decl.default_data != NULL &&
                node->field_decl.default_data->kind == AST_SEND) {
                ASTNode* send_node = node->field_decl.default_data;
                if (send_node->send.args && send_node->send.args->kind == AST_BLOCK) {
                    c->current_class = klass;
                    c->is_method = true;
                    ObjFunction* method_fn = compile_block(c, send_node->send.args);
                    c->current_class = NULL;
                    c->is_method = false;
    
                    add_method(klass, member_name, make_obj((Obj*)method_fn));
                    add_method_type(klass, member_name, build_function_type_from_block(c, send_node->send.args));
                    if (from_class != NULL) {
                        add_method_from(klass, member_name, from_class);
                    }
                    add_or_override_vtable_slot(klass, member_name, make_obj((Obj*)method_fn));
                    return;
                }
            }

            if (from_class != NULL) {
                Value method_val;
                if (find_method(from_class, member_name, &method_val)) {
                    // ★★★ 実体を直入れする代わりに、ラッパー関数を合成して登録 ★★★
                    TypeInfo* m_type = find_method_type(from_class, member_name);
                    ObjFunction* wrapper = create_forwarding_wrapper(c, klass, from_class, member_name, m_type, node);
                    if (wrapper != NULL) {
                        add_method(klass, member_name, make_obj((Obj*)wrapper));
                        if (m_type != NULL) {
                            add_method_type(klass, member_name, m_type);
                        }
                        add_method_from(klass, member_name, from_class);
                        add_or_override_vtable_slot(klass, member_name, make_obj((Obj*)wrapper));
                    }
                    return;
                    // ★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★
                }else {
                    error_compile(node, ERR_DELEGATE_METHOD_NOT_FOUND, 
                            member_name->chars, from_class->name->chars);
                }
            } else {
                error_compile(node, ERR_METHOD_NO_IMPL,
                        member_name->chars);
            }
        }

        
        FieldInfo f_info = new_field_info(member_name, member_type, from_class, is_static);
        Value init_val = make_nil();

        if (node->field_decl.default_data != NULL) {
            if (node->field_decl.default_data->kind == AST_LITERAL) {
                TypeInfo* actual_type = NULL;
                init_val = parse_ast_literal_with_expected(node->field_decl.default_data, member_type, &actual_type);
                
                // ★ フィールド宣言型と初期値リテラルの型チェック！
                if (!is_assignable(c, member_type, actual_type)) {
                    error_compile(node, ERR_TYPE_MISMATCH,
                                  member_type ? member_type->name->chars : "unknown",
                                  actual_type ? actual_type->name->chars : "nil");
                }
            } else {
                // リテラル以外の式（1 + 2 など）は現仕様で非対応ならエラーにする
                error_compile(node, ERR_FIELD_INIT_MUST_BE_LITERAL, member_name->chars);
                init_val = get_implicit_default_value(member_type ? member_type->name : NULL);
            }
        } else {
            init_val = get_implicit_default_value(member_type ? member_type->name : NULL);
        }

        add_field(klass, f_info, init_val);
    }

    if (node->kind == AST_SEND) {
        Symbol* msg_sym = node->send.message->identifier.name;
        if (msg_sym != sym_is && msg_sym != sym_are) return;

        ASTNode* receiver = node->send.receiver;
        ASTNode* args = node->send.args;
    
        Symbol* target_name = NULL;
        ASTNode*   type_node   = NULL;
        ASTNode*   from_node   = NULL;
        bool       is_static   = false;

        if (receiver->kind == AST_FIELD_DECL) {
            target_name = receiver->var_decl.identifier->identifier.name;
            type_node   = receiver->var_decl.type;
            from_node   = receiver->field_decl.from_class;
            is_static   = receiver->var_decl.is_static;
        } else if (receiver->kind == AST_VAR_DECL) {
            target_name = receiver->var_decl.identifier->identifier.name;
            type_node   = receiver->var_decl.type;
            is_static   = receiver->var_decl.is_static;
        } else if (receiver->kind == AST_IDENTIFIER) {
            target_name = receiver->identifier.name;
        }
        
        if (target_name == NULL) return;
        
        ClassDef* from_class = NULL;
        if (from_node != NULL) {
            Symbol* from_name = get_class_name_from_node(from_node);
            from_class  = resolve_class(c, from_name);
            if (from_class == NULL) {
                error_compile(node, ERR_DELEGATE_CLASS_NOT_FOUND,
                        from_name ? from_name->chars : "null", target_name->chars );
                
            }

            if (from_class->state != CLASS_STATE_COMPILED) {
                ASTNode* from_ast = find_class_ast(from_name);
                if (from_ast != NULL) {
                    compile_class_body(c, from_ast);
                }
            }
        }

        if (args && args->kind == AST_BLOCK) {
            // 1. 型シグネチャを登録
            add_method_type(klass, target_name, build_function_type_from_block(c, args));

            if (args->block.body != NULL) {
                // 通常の JapEng 実装メソッド
                c->current_class = klass;
                ObjFunction* method_fn = compile_block(c, args);
                c->current_class = NULL;
                
                // methods テーブルに登録
                add_method(klass, target_name, make_obj((Obj*)method_fn));
                if (from_class != NULL) {
                    add_method_from(klass, target_name, from_class);
                }
                // 宣言順に vtable スロットへ実体を登録！
                add_or_override_vtable_slot(klass, target_name, make_obj((Obj*)method_fn));
            } else {
                // body == NULL の場合、実体は登録しない（VMネイティブと競合させない！）
                // しかし、vtable のスロット枠（インデックス）だけは宣言順に確実に確保する！
                add_or_override_vtable_slot(klass, target_name, make_nil());
            }
            return;
        }
    
        TypeInfo* t_info = build_type_info(type_node);
        FieldInfo f_info = new_field_info(target_name, t_info, from_class, is_static);
    
        Value init_val = make_nil();
        if (args != NULL) {
            if (args->kind == AST_LITERAL) {
                TypeInfo* actual_type = NULL;
                init_val = parse_ast_literal_with_expected(args, t_info, &actual_type);

                // ★ フィールド宣言型と初期値リテラルの型チェック！
                if (!is_assignable(c, t_info, actual_type)) {
                    error_compile(node, ERR_TYPE_MISMATCH,
                                  t_info ? t_info->name->chars : "unknown",
                                  actual_type ? actual_type->name->chars : "nil");
                }
            } else {
                error_compile(node, ERR_FIELD_INIT_MUST_BE_LITERAL, target_name->chars);
                init_val = get_implicit_default_value(t_info ? t_info->name : NULL);
            }
        } else {
            init_val = get_implicit_default_value(t_info ? t_info->name : NULL);
        }
    
        add_field(klass, f_info, init_val);
        return;
    }
}

void declare_class_skeleton(Compiler* c, ASTNode* node)
{
    if (node == NULL) return;

    if (node->kind == AST_STMT) {
        declare_class_skeleton(c, node->stmt.left);
        declare_class_skeleton(c, node->stmt.right);
        return;
    }

    if (node->kind != AST_CLASS_DEF) return;

    ASTNode* class_node = node->class_def.class;
    Symbol* class_name = class_node->class.classname->identifier.name;
    TypeInfo* class_type = build_type_info(class_node);

    ClassDef* klass = new_class(class_name, class_type, NULL, NULL, 0);
    
    if (c->defined_class == NULL) {
        c->defined_class = new_table(16);
    }
    table_set(c->defined_class, class_name, make_obj((Obj*)klass));
}

void resolve_class_skeleton_relations(Compiler* c, ASTNode* node)
{
    if (node == NULL) return;

    if (node->kind == AST_STMT) {
        resolve_class_skeleton_relations(c, node->stmt.left);
        resolve_class_skeleton_relations(c, node->stmt.right);
        return;
    }

    if (node->kind != AST_CLASS_DEF) return;

    ASTNode* class_node = node->class_def.class;
    Symbol* class_name = class_node->class.classname->identifier.name;
    ClassDef* klass = resolve_class(c, class_name);
    if (klass == NULL) return;

    // 1. スーパークラス (based) の接続
    Symbol* super_name = NULL;
    if (node->class_def.super_class != NULL) {
        super_name = get_class_name_from_node(node->class_def.super_class);
    }

    if (super_name != NULL) {
        klass->superclass = resolve_class(c, super_name);
        klass->superclass_type = build_type_info(node->class_def.super_class);
    } else if (klass->name != sym_Object) {
        klass->superclass = resolve_class(c, sym_Object);
        klass->superclass_type = new_type_info(sym_Object, 0);
    }

    // 2. ★ 委譲元 (from) の接続（ここに追加！）
    int delegate_count = count_classes(node->class_def.from_classes);
    if (delegate_count > 0) {
        klass->delegates = (ClassDef**)malloc(sizeof(ClassDef*) * delegate_count);
        klass->delegate_types = (TypeInfo**)malloc(sizeof(TypeInfo*) * delegate_count);

        int idx = 0;
        collect_delegate_classes(c, node->class_def.from_classes, klass->delegates, &idx);
        klass->delegate_count = idx;

        int type_idx = 0;
        collect_delegate_types(node->class_def.from_classes, klass->delegate_types, &type_idx);
    }
}

// シグネチャとフィールドの先行登録
static void declare_class_member_signatures(Compiler* c, ClassDef* klass, ASTNode* node)
{
    if (node == NULL) return;
    if (node->kind == AST_STMT) {
        declare_class_member_signatures(c, klass, node->stmt.left);
        declare_class_member_signatures(c, klass, node->stmt.right);
        return;
    }

    // 1. send 形式 (name: Function is [...];)
    if (node->kind == AST_SEND) {
        ASTNode* receiver = node->send.receiver;
        ASTNode* args = node->send.args;
        Symbol* target_name = NULL;
        if (receiver->kind == AST_IDENTIFIER) target_name = receiver->identifier.name;
        else if (receiver->kind == AST_FIELD_DECL || receiver->kind == AST_VAR_DECL) {
            target_name = receiver->var_decl.identifier->identifier.name;
        }

        if (target_name && args && args->kind == AST_BLOCK) {
            // ★ 型シグネチャだけを先に登録しておく！
            add_method_type(klass, target_name, build_function_type_from_block(c, args));
        }
        return;
    }

    // 2. field_decl 形式 (name: Function is [...];)
    if (node->kind == AST_FIELD_DECL) {
        Symbol* member_name = node->field_decl.identifier->identifier.name;
        TypeInfo*  member_type = build_type_info(node->field_decl.type);
        if (member_type && member_type->name == sym_Function) {
            if (node->field_decl.default_data && node->field_decl.default_data->kind == AST_SEND) {
                ASTNode* send_node = node->field_decl.default_data;
                if (send_node->send.args && send_node->send.args->kind == AST_BLOCK) {
                    add_method_type(klass, member_name, build_function_type_from_block(c, send_node->send.args));
                }
            }
        }
    }
}

void declare_class_signatures(Compiler* c, ASTNode* node)
{
    if (node == NULL) return;

    if (node->kind == AST_STMT) {
        declare_class_signatures(c, node->stmt.left);
        declare_class_signatures(c, node->stmt.right);
        return;
    }

    if (node->kind != AST_CLASS_DEF) return;

    ASTNode* class_node = node->class_def.class;
    Symbol* class_name = class_node->class.classname->identifier.name;
    ClassDef* klass = resolve_class(c, class_name);
    if (klass == NULL) return;

    // このクラスの全メソッドシグネチャを先行登録！
    declare_class_member_signatures(c, klass, node->class_def.members);
}

void compile_class_body(Compiler* c, ASTNode* node)
{
    if (node == NULL) return;

    if (node->kind == AST_STMT) {
        compile_class_body(c, node->stmt.left);
        compile_class_body(c, node->stmt.right);
        return;
    }

    if (node->kind != AST_CLASS_DEF) return;
    ASTNode* class_node = node->class_def.class;
    Symbol* class_name = class_node->class.classname->identifier.name;

    ClassDef* klass = resolve_class(c, class_name);
    if (klass == NULL) {
        error_compile(node, ERR_CLASS_NOT_REGISTERED, class_name->chars);
        return;
    }

    if (klass->state == CLASS_STATE_COMPILED) return;
    if (klass->state == CLASS_STATE_COMPILING) {
        error_compile(node, ERR_CYCLIC_INHERITANCE, class_name->chars);
        return;
    }

    klass->state = CLASS_STATE_COMPILING;

    // 親クラスのフィールドをコピーするために、親が未コンパイルなら先にコンパイルする
    if (klass->superclass != NULL && klass->superclass->state != CLASS_STATE_COMPILED) {
        ASTNode* super_ast = find_class_ast(klass->superclass->name);
        if (super_ast != NULL) {
            compile_class_body(c, super_ast);
        }
    }

    // 親クラスのフィールドを継承
    if (klass->superclass != NULL) {
        for (int i = 0; i < klass->superclass->field_count; i++) {
            add_field(klass, klass->superclass->field_infos[i], klass->superclass->default_values[i]);
        }
    }

    init_class_vtable_from_super(klass);
    
    // 追加: 委譲元クラス（delegates）用の隠しフィールドを自動追加
    for (int i = 0; i < klass->delegate_count; i++) {
        ClassDef* del_klass = klass->delegates[i];
        if (del_klass == NULL) continue;
        
        // 委譲元クラスが未コンパイルなら先にコンパイル
        if (del_klass->state != CLASS_STATE_COMPILED) {
            ASTNode* del_ast = find_class_ast(del_klass->name);
            if (del_ast != NULL) {
                compile_class_body(c, del_ast);
            }
        }
        
        Symbol* hidden_name = get_delegate_field_name(del_klass);
        if (find_field_index(klass, hidden_name) == -1) {
            TypeInfo* del_type = (klass->delegate_types && klass->delegate_types[i]) 
            ? klass->delegate_types[i] 
            : new_type_info(del_klass->name, 0);
            FieldInfo f_info = {
                .name = hidden_name,
                .type = del_type,
                .from_class = del_klass,
                .is_static = false,
                .is_delegate_instance = true // ★ 委譲インスタンスフラグ
            };
            add_field(klass, f_info, make_nil());
        }
    }
    reserve_class_vtable_slots(klass, node->class_def.members);
    
    ClassDef* enclosing_class = c->current_class;
    c->current_class = klass;

    // ★★★ 1. 先にクラス内の全メソッドシグネチャを先行登録！ ★★★
    // （これで下にある parse_regex を上から呼んでもエラーにならなくなる！）
    declare_class_member_signatures(c, klass, node->class_def.members);

    // ★★★ 2. その後、各メソッドの本体をコンパイル ★★★
    compile_class_members(c, klass, node->class_def.members);

    // 追加: init / destroy が未定義ならデフォルトを自動合成
    if (!is_builtin(klass->name)) {
        Value dummy;
        // 1. init の自動補完
        if (!find_method(klass, sym_init, &dummy)) {
            ObjFunction* def_init = create_default_init_function(c, klass);
            add_method(klass, sym_init, make_obj((Obj*)def_init));
            // シグネチャ: Function(; 自クラス型)
            TypeInfo* rets[1] = { klass->type ? klass->type : new_type_info(klass->name, 0) };
            TypeInfo* init_type = new_func_type_info(0, NULL, 1, rets);
            add_method_type(klass, sym_init, init_type);
            add_or_override_vtable_slot(klass, sym_init, make_obj((Obj*)def_init));
        }
    
        // 2. destroy の自動補完
        if (!find_method(klass, sym_destroy, &dummy)) {
            ObjFunction* def_destroy = create_default_destroy_function(c, klass);
            add_method(klass, sym_destroy, make_obj((Obj*)def_destroy));
            // シグネチャ: Function(; Void)
            TypeInfo* destroy_type = new_func_type_info(0, NULL, 0, NULL);
            add_method_type(klass, sym_destroy, destroy_type);
            add_or_override_vtable_slot(klass, sym_destroy, make_obj((Obj*)def_destroy));
        }
    }


    c->current_class = enclosing_class;

    klass->state = CLASS_STATE_COMPILED;

#if DEBUG_CLASS_WRITE
    fprintf(stderr, "=== Compiled Class: %s ===\n", class_name->chars);
    print_class(klass);
#endif

    if (!is_builtin(class_name)) {
        uint32_t class_const_idx = add_constant(c->chunk, make_obj((Obj*)klass));
        uint32_t name_sym_idx = add_constant(c->chunk, make_obj((Obj*)class_name));
        
        emit_inst(c, OP_CONSTANT, class_const_idx);
        emit_inst(c, OP_SET_GLOBAL, name_sym_idx);
        emit_inst(c, OP_POP, 0);

    } 
    const char* out_dir = c->output_dir ? c->output_dir : ".";
    bool is_building_lib = (strstr(out_dir, "lib") != NULL);

    if (!is_builtin(class_name) || is_building_lib) {
        const char* src_file = node->loc.filename;
        const char* fn_only = src_file ? strrchr(src_file, '/') : NULL;
        fn_only = fn_only ? fn_only + 1 : (src_file ? src_file : "unknown.cd");

        write_class_bcd(out_dir, klass, fn_only);
    }
}

void compile_class_def(Compiler* c, ASTNode* node) 
{
    if (node == NULL) return;

    if (node->kind == AST_STMT) {
        compile_class_def(c, node->stmt.left);
        compile_class_def(c, node->stmt.right);
        return;
    }

    if (node->kind != AST_CLASS_DEF) return;

    ASTNode* class_node = node->class_def.class;
    Symbol* class_name = class_node->class.classname->identifier.name;

    // すでにコンパイル済みならスキップ
    ClassDef* existing = NULL;
    for (Compiler* comp = c; comp != NULL; comp = comp->enclosing) {
        if (comp->defined_class != NULL) {
            Value val;
            if (table_get(comp->defined_class, class_name, &val)) {
                existing = (ClassDef*)as_obj(val);
                break;
            }
        }
    }

    if (existing != NULL && existing->state == CLASS_STATE_COMPILED) {
        return;
    }

    if (existing == NULL) {
        declare_class_skeleton(c, node);
        resolve_class_skeleton_relations(c, node);
        declare_class_signatures(c, node);
    }

    compile_class_body(c, node);
}
