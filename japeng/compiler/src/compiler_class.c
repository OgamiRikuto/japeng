#include "compiler_internal.h"
#include "parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static Symbol* get_class_name_from_node(ASTNode* node)
{
    if (node == NULL) return NULL;
    if (node->kind == AST_IDENTIFIER) return node->identifier.name;
    if (node->kind == AST_CLASS) return node->klass.classname->identifier.name;
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
        ASTNode* class_node = node->class_def.klass;
        Symbol* class_name = class_node->klass.classname->identifier.name;
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

static void get_dir_from_filename(const char* filepath, char* out_dir, size_t max_len)
{
    if (!filepath || filepath[0] == '\0') {
        strncpy(out_dir, ".", max_len - 1);
        out_dir[max_len - 1] = '\0';
        return;
    }
    const char* slash = strrchr(filepath, '/');
    if (slash) {
        size_t len = slash - filepath;
        if (len >= max_len) len = max_len - 1;
        strncpy(out_dir, filepath, len);
        out_dir[len] = '\0';
    } else {
        strncpy(out_dir, ".", max_len - 1);
        out_dir[max_len - 1] = '\0';
    }
}

ClassDef* resolve_class(Compiler* c, Symbol* name)
{
    if (name == NULL) return NULL;

    name = resolve_name_alias(c, name);

    // import された名前（エイリアス含む）を最優先で探索
    for (Compiler* comp = c; comp != NULL; comp = comp->enclosing) {
        if (comp->imported_classes != NULL) {
            Value val;
            if (table_get(comp->imported_classes, name, &val)) {
                return (ClassDef*)as_obj(val);
            }
        }
    }

    // 1. メモリ上のテーブル確認
    for (Compiler* comp = c; comp != NULL; comp = comp->enclosing) {
        if (comp->defined_class != NULL) {
            Value val;
            if (table_get(comp->defined_class, name, &val)) {
                return (ClassDef*)as_obj(val);
            }
        }
    }

    const char* base_dir = c->output_dir ? c->output_dir : ".";
    char bcd_path[1024];
    get_cache_filepath(base_dir, name->chars, "bcd", bcd_path, sizeof(bcd_path));

    // 2. プロジェクトローカルのキャッシュ存在 ＆ 鮮度チェック
    if (is_cache_fresh(bcd_path, base_dir)) {
        ClassDef* loaded = load_class_type_from_bcd(c, bcd_path);
        if (loaded) return loaded;
    }

    // 3. すでにパース済みの AST にある場合
    ASTNode* existing_ast = find_class_ast(name);
    if (existing_ast != NULL) {
        declare_class_skeleton(c, existing_ast);
        resolve_class_skeleton_relations(c, existing_ast);
        declare_class_signatures(c, existing_ast);
        compile_class_body(c, existing_ast);

        for (Compiler* comp = c; comp != NULL; comp = comp->enclosing) {
            if (comp->defined_class != NULL) {
                Value val;
                if (table_get(comp->defined_class, name, &val)) return (ClassDef*)as_obj(val);
            }
        }
    }

    // 4. プロジェクトディレクトリ内のソース探索（最優先）
    char cd_path[1024];
    bool found_source = false;
    struct stat st;

    snprintf(cd_path, sizeof(cd_path), "%s/%s.cd", base_dir, name->chars);
    if (stat(cd_path, &st) == 0) {
        found_source = true;
    } else {
        snprintf(cd_path, sizeof(cd_path), "%s/%s.jcd", base_dir, name->chars);
        if (stat(cd_path, &st) == 0) {
            found_source = true;
        }
    }

    // ローカルで見つかった場合はパースしてコンパイル（compile_class_body 内でキャッシュも自動書き出し）
    if (found_source && parse_file(cd_path)) {
        ASTNode* ast = find_class_ast(name);
        if (ast) {
            declare_class_skeleton(c, ast);
            resolve_class_skeleton_relations(c, ast);
            declare_class_signatures(c, ast);
            compile_class_body(c, ast);

            return resolve_class(c, name);
        }
    }

    // 5. システム組み込みキャッシュ (Object, String 等の暗黙型のみ！)
    // ★ is_builtin の場合だけ lib/.je_cache を見に行く（File 等の横取りを完全遮断）
    if (is_builtin(name)) {
        const char* stdlib_dir = get_japeng_lib_dir();
        char builtin_bcd[1024];
        get_cache_filepath(stdlib_dir, name->chars, "bcd", builtin_bcd, sizeof(builtin_bcd));
        if (is_cache_fresh(builtin_bcd, stdlib_dir)) {
            ClassDef* loaded = load_class_type_from_bcd(c, builtin_bcd);
            if (loaded) return loaded;
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

static int count_interfaces(ASTNode* node) {
    if (node == NULL) return 0;
    if (node->kind == AST_STMT) {
        return count_interfaces(node->stmt.left) + count_interfaces(node->stmt.right);
    }
    return 1;
}

static void collect_interfaces(Compiler* c, ASTNode* node, ClassDef** list, int* idx) {
    if (node == NULL) return;
    if (node->kind == AST_STMT) {
        collect_interfaces(c, node->stmt.left, list, idx);
        collect_interfaces(c, node->stmt.right, list, idx);
        return;
    }
    Symbol* name = get_class_name_from_node(node);
    ClassDef* iface = resolve_class(c, name);
    if (iface != NULL) {
        list[(*idx)++] = iface;
    }
}

static void collect_interface_types(Compiler* c, ASTNode* node, TypeInfo** list, int* idx) {
    if (node == NULL) return;
    if (node->kind == AST_STMT) {
        collect_interface_types(c, node->stmt.left, list, idx);
        collect_interface_types(c, node->stmt.right, list, idx);
        return;
    }
    list[(*idx)++] = build_type_info(c, node);
}

static void collect_delegate_types(Compiler* c, ASTNode* node, TypeInfo** list, int* idx)
{
    if (node == NULL) return;
    if (node->kind == AST_STMT) {
        collect_delegate_types(c, node->stmt.left, list, idx);
        collect_delegate_types(c, node->stmt.right, list, idx);
        return;
    }
    list[(*idx)++] = build_type_info(c, node);
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

static void fill_type_args(Compiler* c, ASTNode* node, TypeInfo** args, int* index)
{
    if (node == NULL) return;
    if (node->kind == AST_STMT) {
        fill_type_args(c, node->stmt.left, args, index);
        fill_type_args(c, node->stmt.right, args, index);
        return;
    }
    args[(*index)++] = build_type_info(c, node);
}

static void add_interface_if_missing(ClassDef* target_class, ClassDef* iface, TypeInfo* iface_type) {
    if (iface == NULL) return;
    for (int i = 0; i < target_class->interface_count; i++) {
        if (target_class->interfaces[i] == iface) return;
    }
    target_class->interfaces = (ClassDef**)realloc(
        target_class->interfaces, sizeof(ClassDef*) * (target_class->interface_count + 1));
    target_class->interface_types = (TypeInfo**)realloc(
        target_class->interface_types, sizeof(TypeInfo*) * (target_class->interface_count + 1));

    target_class->interfaces[target_class->interface_count] = iface;
    target_class->interface_types[target_class->interface_count] = iface_type;
    target_class->interface_count++;
}

static void collect_all_ancestor_interfaces(ClassDef* target_class, ClassDef* iface) {
    if (iface == NULL) return;
    for (int i = 0; i < iface->interface_count; i++) {
        ClassDef* parent_iface = iface->interfaces[i];
        TypeInfo* parent_type = iface->interface_types ? iface->interface_types[i] : NULL;
        if (parent_iface != NULL) {
            add_interface_if_missing(target_class, parent_iface, parent_type);
            collect_all_ancestor_interfaces(target_class, parent_iface);
        }
    }
}

TypeInfo* build_type_info(Compiler* c, ASTNode* node)
{
    if (node == NULL) return NULL;

    if (node->kind == AST_CLASS) {
        Symbol* raw_name = node->klass.classname->identifier.name;
        Symbol* name = resolve_name_alias(c, raw_name);
        int arg_count = count_type_args(node->klass.types);

        TypeInfo* t_info = new_type_info(name, arg_count);
        if (arg_count > 0) {
            int idx = 0;
            fill_type_args(c, node->klass.types, t_info->type_args, &idx);
        }
        return t_info;
    }

    if (node->kind == AST_FUNC_TYPE) {
        TypeInfo* params[16]; int p_cnt = 0;
        TypeInfo* rets[16];   int r_cnt = 0;
        if (node->func_type.params) collect_ret_types_from_ast(c, node->func_type.params, params, &p_cnt);
        if (node->func_type.rets)   collect_ret_types_from_ast(c, node->func_type.rets, rets, &r_cnt);

        return new_func_type_info(p_cnt, params, r_cnt, rets);
    }

    if (node->kind == AST_IDENTIFIER) {
        Symbol* raw_name = node->identifier.name;
        Symbol* name = resolve_name_alias(c, raw_name);
        return new_type_info(name, 0);
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

    write_chunk(chunk, make_inst(OP_GET_LOCAL, 0));
    write_chunk(chunk, make_inst(OP_GET_FIELD, (uint32_t)del_field_idx));

    for (uint8_t i = 1; i <= arity; i++) {
        write_chunk(chunk, make_inst(OP_GET_LOCAL, (uint32_t)i));
    }

    int vtable_idx = get_vtable_index(from_class, member_name);
    write_chunk(chunk, make_virtual_call_inst(arity, (uint16_t)vtable_idx));

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

static ObjFunction* create_default_init_function(Compiler* c, ClassDef* klass)
{
    Chunk* chunk = (Chunk*)malloc(sizeof(Chunk));
    init_chunk(chunk);

    if (klass->superclass != NULL) {
        Value super_init_val;
        if (find_method(klass->superclass, sym_init, &super_init_val)) {
            write_chunk(chunk, make_inst(OP_GET_LOCAL, 0));
            uint32_t fn_const = add_constant(chunk, super_init_val);
            write_chunk(chunk, make_super_call_inst(0, (uint16_t)fn_const));
            write_chunk(chunk, make_inst(OP_POP, 0));
        }
    }

    write_chunk(chunk, make_inst(OP_GET_LOCAL, 0));
    write_chunk(chunk, make_inst(OP_RETURN, 1));

    ObjFunction* fn = new_function(chunk, 0);
    fn->upvalue_count = 0;
    fn->upvalues = NULL;
    return fn;
}

static ObjFunction* create_default_destroy_function(Compiler* c, ClassDef* klass)
{
    Chunk* chunk = (Chunk*)malloc(sizeof(Chunk));
    init_chunk(chunk);

    if (klass->superclass != NULL) {
        Value super_destroy_val;
        if (find_method(klass->superclass, sym_destroy, &super_destroy_val)) {
            write_chunk(chunk, make_inst(OP_GET_LOCAL, 0));
            uint32_t fn_const = add_constant(chunk, super_destroy_val);
            write_chunk(chunk, make_super_call_inst(0, (uint16_t)fn_const));
            write_chunk(chunk, make_inst(OP_POP, 0));
        }
    }

    write_chunk(chunk, make_inst(OP_RETURN, 0));

    ObjFunction* fn = new_function(chunk, 0);
    fn->upvalue_count = 0;
    fn->upvalues = NULL;
    return fn;
}

static void reserve_class_vtable_slots(Compiler* c, ClassDef* klass, ASTNode* node)
{
    if (node == NULL) return;
    if (node->kind == AST_STMT) {
        reserve_class_vtable_slots(c, klass, node->stmt.left);
        reserve_class_vtable_slots(c, klass, node->stmt.right);
        return;
    }

    if (node->kind == AST_SEND) {
        ASTNode* receiver = node->send.receiver;
        ASTNode* args = node->send.args;
        Symbol* target_name = NULL;
        bool is_static = false;
        if (receiver->kind == AST_IDENTIFIER) target_name = receiver->identifier.name;
        else if (receiver->kind == AST_FIELD_DECL || receiver->kind == AST_VAR_DECL) {
            target_name = receiver->var_decl.identifier->identifier.name;
            is_static = receiver->var_decl.is_static;
        }
        if (!is_static && target_name && args && args->kind == AST_BLOCK) {
            add_or_override_vtable_slot(klass, target_name, make_nil());
        }
        return;
    }

    if (node->kind == AST_FIELD_DECL) {
        if (node->field_decl.is_static) return;
        Symbol* member_name = node->field_decl.identifier->identifier.name;
        TypeInfo* member_type = build_type_info(c, node->field_decl.type);
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
        TypeInfo*  member_type = build_type_info(c, node->field_decl.type);
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
            from_class = resolve_class(c, from_name);
            if (from_class == NULL) {
                error_compile(node, ERR_DELEGATE_CLASS_NOT_FOUND,
                        from_name ? from_name->chars : "null", member_name->chars );
            }

            if (from_class && from_class->state != CLASS_STATE_COMPILED) {
                ASTNode* from_ast = find_class_ast(from_name);
                if (from_ast != NULL) {
                    compile_class_body(c, from_ast);
                }
            }
        }

        bool is_function = (member_type != NULL && member_type->name != NULL &&
                            member_type->name == sym_Function);
        
        if (is_function) {
            if (klass->is_interface) {
                add_or_override_vtable_slot(klass, member_name, make_nil());
                return;
            }
            if (node->field_decl.default_data != NULL &&
                node->field_decl.default_data->kind == AST_SEND) {
                ASTNode* send_node = node->field_decl.default_data;
                if (send_node->send.args && send_node->send.args->kind == AST_BLOCK) {
                    c->current_class = klass;
                    c->is_method = true;
                    c->is_static_method = is_static;
                    ObjFunction* method_fn = compile_block(c, send_node->send.args);
                    c->current_class = NULL;
                    c->is_method = false;
                    c->is_static_method = false;
    
                    if (is_static) {
                        add_static_method(klass, member_name, make_obj((Obj*)method_fn));
                        add_method_type(klass, member_name, build_function_type_from_block(c, send_node->send.args));
                        return;
                    }

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
                } else {
                    error_compile(node, ERR_DELEGATE_METHOD_NOT_FOUND, 
                            member_name->chars, from_class->name->chars);
                }
            } else {
                error_compile(node, ERR_METHOD_NO_IMPL, member_name->chars);
            }
        }

        FieldInfo f_info = new_field_info(member_name, member_type, from_class, is_static);
        Value init_val = make_nil();

        if (node->field_decl.default_data != NULL) {
            if (node->field_decl.default_data->kind == AST_LITERAL) {
                TypeInfo* actual_type = NULL;
                init_val = parse_ast_literal_with_expected(node->field_decl.default_data, member_type, &actual_type);
                
                if (!is_assignable(c, member_type, actual_type)) {
                    error_compile(node, ERR_TYPE_MISMATCH,
                                  member_type ? member_type->name->chars : "unknown",
                                  actual_type ? actual_type->name->chars : "nil");
                }
            } else {
                error_compile(node, ERR_FIELD_INIT_MUST_BE_LITERAL, member_name->chars);
                init_val = get_implicit_default_value(member_type ? member_type->name : NULL);
            }
        } else {
            init_val = get_implicit_default_value(member_type ? member_type->name : NULL);
        }

        if (is_static) {
            add_static_field(klass, f_info, init_val);
        } else {
            add_field(klass, f_info, init_val);
        }
    }

    if (node->kind == AST_SEND) {
        Symbol* msg_sym = node->send.message->identifier.name;
        if (msg_sym != sym_is && msg_sym != sym_are) return;

        ASTNode* receiver = node->send.receiver;
        ASTNode* args = node->send.args;
    
        Symbol* target_name = NULL;
        ASTNode* type_node   = NULL;
        ASTNode* from_node   = NULL;
        bool is_static       = false;

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
            from_class = resolve_class(c, from_name);
            if (from_class == NULL) {
                error_compile(node, ERR_DELEGATE_CLASS_NOT_FOUND,
                        from_name ? from_name->chars : "null", target_name->chars );
            }

            if (from_class && from_class->state != CLASS_STATE_COMPILED) {
                ASTNode* from_ast = find_class_ast(from_name);
                if (from_ast != NULL) {
                    compile_class_body(c, from_ast);
                }
            }
        }

        if (args && args->kind == AST_BLOCK) {
            add_method_type(klass, target_name, build_function_type_from_block(c, args));

            if (args->block.body != NULL) {
                c->current_class = klass;
                c->is_method = true;
                c->is_static_method = is_static;
                ObjFunction* method_fn = compile_block(c, args);
                c->current_class = NULL;
                c->is_method = false;
                c->is_static_method = false;
                
                if (is_static) {
                    add_static_method(klass, target_name, make_obj((Obj*)method_fn));
                    return;
                }

                add_method(klass, target_name, make_obj((Obj*)method_fn));
                if (from_class != NULL) {
                    add_method_from(klass, target_name, from_class);
                }
                add_or_override_vtable_slot(klass, target_name, make_obj((Obj*)method_fn));
            } else {
                if (!is_static) {
                    add_or_override_vtable_slot(klass, target_name, make_nil());
                }
            }
            return;
        }
    
        TypeInfo* t_info = build_type_info(c, type_node);
        FieldInfo f_info = new_field_info(target_name, t_info, from_class, is_static);
    
        Value init_val = make_nil();
        if (args != NULL) {
            if (args->kind == AST_LITERAL) {
                TypeInfo* actual_type = NULL;
                init_val = parse_ast_literal_with_expected(args, t_info, &actual_type);

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
    
        if (is_static) {
            add_static_field(klass, f_info, init_val);
        } else {
            add_field(klass, f_info, init_val);
        }
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

    ASTNode* class_node = node->class_def.klass;
    Symbol* class_name = class_node->klass.classname->identifier.name;
    TypeInfo* class_type = build_type_info(c, class_node);

    ClassDef* klass = new_class(class_name, class_type, NULL, NULL, 0);
    klass->is_interface = node->class_def.is_interface;
    
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

    ASTNode* class_node = node->class_def.klass;
    Symbol* class_name = class_node->klass.classname->identifier.name;
    ClassDef* klass = resolve_class(c, class_name);
    if (klass == NULL) return;

    if (klass->is_interface) {
        klass->superclass = NULL;
        klass->superclass_type = NULL;

        if (node->class_def.super_class != NULL) {
            int iface_count = count_interfaces(node->class_def.super_class);
            if (iface_count > 0) {
                klass->interfaces = (ClassDef**)malloc(sizeof(ClassDef*) * iface_count);
                klass->interface_types = (TypeInfo**)malloc(sizeof(TypeInfo*) * iface_count);

                int idx = 0;
                collect_interfaces(c, node->class_def.super_class, klass->interfaces, &idx);
                klass->interface_count = idx;

                int type_idx = 0;
                collect_interface_types(c, node->class_def.super_class, klass->interface_types, &type_idx);
            }
        }
        return;
    }

    // 1. スーパークラス (based) の接続
    Symbol* super_name = NULL;
    if (node->class_def.super_class != NULL) {
        super_name = get_class_name_from_node(node->class_def.super_class);
    }

    if (super_name != NULL) {
        klass->superclass = resolve_class(c, super_name);
        klass->superclass_type = build_type_info(c, node->class_def.super_class);
    } else if (klass->name != sym_Object) {
        klass->superclass = resolve_class(c, sym_Object);
        klass->superclass_type = new_type_info(sym_Object, 0);
    }

    // 2. 委譲元 (from) の接続
    int delegate_count = count_classes(node->class_def.from_classes);
    if (delegate_count > 0) {
        klass->delegates = (ClassDef**)malloc(sizeof(ClassDef*) * delegate_count);
        klass->delegate_types = (TypeInfo**)malloc(sizeof(TypeInfo*) * delegate_count);

        int idx = 0;
        collect_delegate_classes(c, node->class_def.from_classes, klass->delegates, &idx);
        klass->delegate_count = idx;

        int type_idx = 0;
        collect_delegate_types(c, node->class_def.from_classes, klass->delegate_types, &type_idx);
    }

    // 3. インターフェース
    if (node->class_def.interfaces != NULL) {
        int iface_count = count_interfaces(node->class_def.interfaces);
        if (iface_count > 0) {
            klass->interfaces = (ClassDef**)malloc(sizeof(ClassDef*) * iface_count);
            klass->interface_types = (TypeInfo**)malloc(sizeof(TypeInfo*) * iface_count);

            int idx = 0;
            collect_interfaces(c, node->class_def.interfaces, klass->interfaces, &idx);
            klass->interface_count = idx;

            int type_idx = 0;
            collect_interface_types(c, node->class_def.interfaces, klass->interface_types, &type_idx);
        }
    }
}

static void declare_class_member_signatures(Compiler* c, ClassDef* klass, ASTNode* node)
{
    if (node == NULL) return;
    if (node->kind == AST_STMT) {
        declare_class_member_signatures(c, klass, node->stmt.left);
        declare_class_member_signatures(c, klass, node->stmt.right);
        return;
    }

    if (node->kind == AST_SEND) {
        ASTNode* receiver = node->send.receiver;
        ASTNode* args = node->send.args;
        Symbol* target_name = NULL;
        if (receiver->kind == AST_IDENTIFIER) target_name = receiver->identifier.name;
        else if (receiver->kind == AST_FIELD_DECL || receiver->kind == AST_VAR_DECL) {
            target_name = receiver->var_decl.identifier->identifier.name;
        }

        if (target_name && args && args->kind == AST_BLOCK) {
            add_method_type(klass, target_name, build_function_type_from_block(c, args));
        }
        return;
    }

    if (node->kind == AST_FIELD_DECL) {
        Symbol* member_name = node->field_decl.identifier->identifier.name;
        TypeInfo* member_type = build_type_info(c, node->field_decl.type);

        if (klass->is_interface) {
            if (member_type == NULL || member_type->name != sym_Function) {
                error_compile(node, ERR_INTERFACE_CANNOT_HAVE_FIELDS, klass->name->chars, member_name->chars);
                return;
            }
            add_method_type(klass, member_name, member_type);
            add_or_override_vtable_slot(klass, member_name, make_nil());
            return;
        }

        if (member_type && member_type->name == sym_Function) {
            if (node->field_decl.default_data && node->field_decl.default_data->kind == AST_SEND) {
                ASTNode* send_node = node->field_decl.default_data;
                if (send_node->send.args && send_node->send.args->kind == AST_BLOCK) {
                    if (klass->is_interface && send_node->send.args->block.body != NULL) {
                        error_compile(node, ERR_INTERFACE_METHOD_CANNOT_HAVE_BODY, member_name->chars, klass->name->chars);
                        return;
                    }
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

    ASTNode* class_node = node->class_def.klass;
    Symbol* class_name = class_node->klass.classname->identifier.name;
    ClassDef* klass = resolve_class(c, class_name);
    if (klass == NULL) return;

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
    ASTNode* class_node = node->class_def.klass;
    Symbol* class_name = class_node->klass.classname->identifier.name;

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

    if (klass->superclass != NULL && klass->superclass->state != CLASS_STATE_COMPILED) {
        ASTNode* super_ast = find_class_ast(klass->superclass->name);
        if (super_ast != NULL) {
            compile_class_body(c, super_ast);
        }
    }

    for (int i = 0; i < klass->interface_count; i++) {
        ClassDef* iface = klass->interfaces[i];
        if (iface == NULL) continue;
        if (iface->state != CLASS_STATE_COMPILED) {
            ASTNode* iface_ast = find_class_ast(iface->name);
            if (iface_ast != NULL) {
                compile_class_body(c, iface_ast);
            }
        }
    }

    if (klass->superclass != NULL && klass->superclass->interface_count > 0) {
        ClassDef* super = klass->superclass;
        for (int i = 0; i < super->interface_count; i++) {
            ClassDef* parent_iface = super->interfaces[i];
            TypeInfo* parent_iface_type = super->interface_types ? super->interface_types[i] : NULL;
            if (parent_iface == NULL) continue;

            bool already_has = false;
            for (int j = 0; j < klass->interface_count; j++) {
                if (klass->interfaces[j] == parent_iface) {
                    already_has = true;
                    break;
                }
            }

            if (!already_has) {
                klass->interfaces = (ClassDef**)realloc(
                    klass->interfaces, sizeof(ClassDef*) * (klass->interface_count + 1));
                klass->interface_types = (TypeInfo**)realloc(
                    klass->interface_types, sizeof(TypeInfo*) * (klass->interface_count + 1));

                klass->interfaces[klass->interface_count] = parent_iface;
                klass->interface_types[klass->interface_count] = parent_iface_type;
                klass->interface_count++;
            }
        }
    }

    if (klass->superclass != NULL) {
        for (int i = 0; i < klass->superclass->field_count; i++) {
            add_field(klass, klass->superclass->field_infos[i], klass->superclass->default_values[i]);
        }
    }

    if (!klass->is_interface) {
        init_class_vtable_from_super(klass);
    }
    
    for (int i = 0; i < klass->delegate_count; i++) {
        ClassDef* del_klass = klass->delegates[i];
        if (del_klass == NULL) continue;
        
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
                .is_delegate_instance = true
            };
            add_field(klass, f_info, make_nil());
        }
    }
    reserve_class_vtable_slots(c, klass, node->class_def.members);
    
    ClassDef* enclosing_class = c->current_class;
    c->current_class = klass;

    declare_class_member_signatures(c, klass, node->class_def.members);
    compile_class_members(c, klass, node->class_def.members);

    if (!is_builtin(klass->name) && !klass->is_interface) {
        Value dummy;
        if (!find_method(klass, sym_init, &dummy) && !find_method_type(klass, sym_init)) {
            ObjFunction* def_init = create_default_init_function(c, klass);
            add_method(klass, sym_init, make_obj((Obj*)def_init));
            TypeInfo* rets[1] = { klass->type ? klass->type : new_type_info(klass->name, 0) };
            TypeInfo* init_type = new_func_type_info(0, NULL, 1, rets);
            add_method_type(klass, sym_init, init_type);
            add_or_override_vtable_slot(klass, sym_init, make_obj((Obj*)def_init));
        }
    
        if (!find_method(klass, sym_destroy, &dummy) && !find_method_type(klass, sym_destroy)) {
            ObjFunction* def_destroy = create_default_destroy_function(c, klass);
            add_method(klass, sym_destroy, make_obj((Obj*)def_destroy));
            TypeInfo* destroy_type = new_func_type_info(0, NULL, 0, NULL);
            add_method_type(klass, sym_destroy, destroy_type);
            add_or_override_vtable_slot(klass, sym_destroy, make_obj((Obj*)def_destroy));
        }
    }

    c->current_class = enclosing_class;

    if (klass->is_interface && klass->interface_count > 0) {
        for (int i = 0; i < klass->interface_count; i++) {
            ClassDef* parent_iface = klass->interfaces[i];
            if (parent_iface == NULL) continue;

            for (int m = 0; m < parent_iface->vtable_count; m++) {
                Symbol* mname = parent_iface->vtable_names[m];
                TypeInfo* mtype = find_method_type(parent_iface, mname);
                if (find_method_type(klass, mname) == NULL) {
                    add_method_type(klass, mname, mtype);
                    add_or_override_vtable_slot(klass, mname, make_nil());
                }
            }
        }
    }

    if (!klass->is_interface && klass->interface_count > 0) {
        int initial_count = klass->interface_count;
        for (int i = 0; i < initial_count; i++) {
            collect_all_ancestor_interfaces(klass, klass->interfaces[i]);
        }
    }

    if (!klass->is_interface && klass->interface_count > 0) {
        for (int i = 0; i < klass->interface_count; i++) {
            ClassDef* iface = klass->interfaces[i];
            if (iface == NULL || iface->method_types == NULL) continue;

            for (int k = 0; k < iface->method_types->capacity; k++) {
                HashEntry* entry = &iface->method_types->entries[k];
                if (entry->status != OCCUPIED || entry->key == NULL) continue;

                Symbol* req_method_name = entry->key;
                TypeInfo* req_type = (TypeInfo*)as_obj(entry->value);

                TypeInfo* actual_type = find_method_type(klass, req_method_name);
                if (actual_type == NULL) {
                    error_compile(node, ERR_INTERFACE_NOT_IMPLEMENTED,
                                  klass->name->chars, req_method_name->chars, iface->name->chars);
                    continue;
                }

                if (!is_assignable(c, req_type, actual_type)) {
                    error_compile(node, ERR_INTERFACE_METHOD_CONFLICT,
                                  req_method_name->chars, iface->name->chars, klass->name->chars);
                }
            }
        }
    }

    klass->state = CLASS_STATE_COMPILED;
    fprintf(stderr, "\033[1;36m[COMPILED CLASS]\033[0m %s (file: %s)\n", 
            class_name->chars, node->loc.filename ? node->loc.filename : "unknown");

    if (!is_builtin(class_name)) {
        uint32_t class_const_idx = add_constant(c->chunk, make_obj((Obj*)klass));
        uint32_t name_sym_idx = add_constant(c->chunk, make_obj((Obj*)class_name));
        
        emit_inst(c, OP_CONSTANT, class_const_idx);
        emit_inst(c, OP_SET_GLOBAL, name_sym_idx);
        emit_inst(c, OP_POP, 0);
    } 

    // 出力先を「そのファイルがある親ディレクトリ」に決定
    char target_cache_dir[1024];
    get_dir_from_filename(node->loc.filename, target_cache_dir, sizeof(target_cache_dir));

    bool is_building_lib = (strstr(target_cache_dir, "lib") != NULL);

    if (!is_builtin(class_name) || is_building_lib) {
        const char* src_file = node->loc.filename;
        const char* fn_only = src_file ? strrchr(src_file, '/') : NULL;
        fn_only = fn_only ? fn_only + 1 : (src_file ? src_file : "unknown.cd");

        write_class_bcd(target_cache_dir, klass, fn_only);
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

    ASTNode* class_node = node->class_def.klass;
    Symbol* class_name = class_node->klass.classname->identifier.name;

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

ClassDef* resolve_class_from_file(Compiler* c, const char* file_path)
{
    if (!file_path) return NULL;

    // ファイル名からクラス名シンボルを特定 (例: lib/std/File/File.cd -> File)
    const char* slash = strrchr(file_path, '/');
    const char* base_name = slash ? slash + 1 : file_path;
    char class_name_buf[128];
    strncpy(class_name_buf, base_name, sizeof(class_name_buf) - 1);
    char* dot = strrchr(class_name_buf, '.');
    if (dot) *dot = '\0';
    Symbol* class_sym = intern_cstr(class_name_buf);

    // そのファイルが存在する親ディレクトリを取得
    char file_dir[1024];
    get_dir_from_filename(file_path, file_dir, sizeof(file_dir));

    // 1. ファイル自身の横にある .je_cache/<Name>.bcd を最優先チェック！
    char bcd_path[1024];
    get_cache_filepath(file_dir, class_sym->chars, "bcd", bcd_path, sizeof(bcd_path));
    if (is_cache_fresh(bcd_path, file_dir)) {
        ClassDef* loaded = load_class_type_from_bcd(c, bcd_path);
        if (loaded) return loaded;
    }

    // 2. キャッシュがない／古い場合はパースしてコンパイル
    if (!parse_file(file_path)) {
        return NULL;
    }

    ASTNode* ast = find_class_ast(class_sym);
    if (ast) {
        const char* saved_filename = c->filename;
        c->filename = file_path;
        declare_class_skeleton(c, ast);
        resolve_class_skeleton_relations(c, ast);
        declare_class_signatures(c, ast);
        compile_class_body(c, ast);
        c->filename = saved_filename;
        return resolve_class(c, class_sym);
    }
    return NULL;
}
