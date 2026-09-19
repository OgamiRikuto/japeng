#include "compiler_internal.h"

#include <stdio.h>
#include <stdlib.h>

static ObjString* get_class_name_from_node(ASTNode* node)
{
    if (node == NULL) return NULL;
    if (node->kind == AST_IDENTIFIER) return node->identifier.name;
    if (node->kind == AST_CLASS) return node->class.classname->identifier.name;
    return NULL;
}

static ObjClass* resolve_class(Compiler* c, ObjString* name)
{
    if (name == NULL) return NULL;
    Value val;
    if (c->defined_class != NULL && table_get(c->defined_class, name, &val)) {
        return (ObjClass*)as_obj(val);
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

static void collect_delegate_classes(Compiler* c, ASTNode* node, ObjClass** list, int* idx)
{
    if (node == NULL) return;
    if (node->kind == AST_STMT) {
        collect_delegate_classes(c, node->stmt.left, list, idx);
        collect_delegate_classes(c, node->stmt.right, list, idx);
        return;
    }
    ObjString* name = get_class_name_from_node(node);
    ObjClass* klass = resolve_class(c, name);
    if (klass != NULL) {
        list[(*idx)++] = klass;
    }
}

static int count_type_args(ASTNode* node)
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
        ObjString* name = node->class.classname->identifier.name;
        int arg_count = count_type_args(node->class.types);

        TypeInfo* t_info = new_type_info(name, arg_count);
        if (arg_count > 0) {
            int idx = 0;
            fill_type_args(node->class.types, t_info->type_args, &idx);
        }
        return t_info;
    }

    if (node->kind == AST_IDENTIFIER) {
        return new_type_info(node->identifier.name, 0);
    }

    return NULL;
}

static Value get_implicit_default_value(ObjString* type_name)
{
    if (is_integer_type(type_name)) return make_int(0);
    if (is_float_type(type_name)) return make_float(0.0);
    return make_nil();
}

extern ASTNode* parsed_cd[];
extern int      parsed_cd_count;

static ASTNode* find_class_ast(ObjString* name)
{
    if (name == NULL) return NULL;
    for (int index = 0; index < parsed_cd_count; index++) {
        ASTNode* node = parsed_cd[index];
        if (node != NULL && node->kind == AST_CLASS_DEF) {
            ASTNode* class_node = node->class_def.class;
            ObjString* class_name = class_node->class.classname->identifier.name;
            if (class_name == name) return node;
        }
    }
    return NULL;
}

static void compile_class_members(Compiler* c, ObjClass* klass, ASTNode* node)
{
    if (node == NULL) return;

    if (node->kind == AST_STMT) {
        compile_class_members(c, klass, node->stmt.left);
        compile_class_members(c, klass, node->stmt.right);
        return;
    }

    if (node->kind == AST_FIELD_DECL) {
        ObjString* member_name = node->field_decl.identifier->identifier.name;
        TypeInfo*  member_type = build_type_info(node->field_decl.type);
        ASTNode*   from_node   = node->field_decl.from_class;
        bool       is_static   = node->field_decl.is_static;

        ObjClass* from_class = NULL;
        if (from_node != NULL) {
            ObjString* from_name = get_class_name_from_node(from_node);
            from_class  = resolve_class(c, from_name);
            if (from_class == NULL) {
                fprintf(stderr, "Error: Delegate class '%s' node found for field '%s'.\n",
                        from_name ? from_name->chars : "null", member_name->chars );
                exit(EXIT_FAILURE);
            }

            if (from_class->state != CLASS_STATE_COMPILED) {
                ASTNode* from_ast = find_class_ast(from_name);
                if (from_ast != NULL) {
                    compile_class_body(c, from_ast);
                }
            }
        }

        bool is_function = (member_type != NULL && member_type->name != NULL &&
                            member_type->name == intern_cstr("Function"));
        
        if (is_function) {
            if (node->field_decl.default_data != NULL &&
                node->field_decl.default_data->kind == AST_SEND) {
                ASTNode* send_node = node->field_decl.default_data;
                if (send_node->send.args && send_node->send.args->kind == AST_BLOCK) {
                    c->current_class = klass;
                    ObjFunction* method_fn = compile_block(c, send_node->send.args);
                    c->current_class = NULL;
    
                    add_method(klass, member_name, make_obj((Obj*)method_fn));
                    if (from_class != NULL) {
                        add_method_from(klass, member_name, from_class);
                    }
                    return;
                }
            }

            if (from_class != NULL) {
                Value method_val;
                if (find_method(from_class, member_name, &method_val)) {
                    add_method(klass, member_name, method_val);
                    add_method_from(klass, member_name, from_class);
                    return;
                } else {
                    fprintf(stderr, "Error: Method '%s' not found in delegate class '%s'.\n", 
                            member_name->chars, from_class->name->chars);
                }
            } else {
                fprintf(stderr, "Error: Method '%s' must have an implementation or a delegate source.\n",
                        member_name->chars);
            }
        }

        
        FieldInfo f_info = new_field_info(member_name, member_type, from_class, is_static);
        Value init_val = make_nil();

        if (node->field_decl.default_data != NULL &&
            node->field_decl.default_data->kind == AST_LITERAL) {
            init_val = node->field_decl.default_data->literal.value;
        } else {
            init_val = get_implicit_default_value(member_type ? member_type->name : NULL);
        }

        add_field(klass, f_info, init_val);
    }

    if (node->kind == AST_SEND) {
        ObjString* msg_sym = node->send.message->identifier.name;
        if (msg_sym != sym_is && msg_sym != sym_are) return;

        ASTNode* receiver = node->send.receiver;
        ASTNode* args = node->send.args;
    
        ObjString* target_name = NULL;
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
        
        ObjClass* from_class = NULL;
        if (from_node != NULL) {
            ObjString* from_name = get_class_name_from_node(from_node);
            from_class  = resolve_class(c, from_name);
            if (from_class == NULL) {
                fprintf(stderr, "Error: Delegate class '%s' node found for field '%s'.\n",
                        from_name ? from_name->chars : "null", target_name->chars );
                exit(EXIT_FAILURE);
            }

            if (from_class->state != CLASS_STATE_COMPILED) {
                ASTNode* from_ast = find_class_ast(from_name);
                if (from_ast != NULL) {
                    compile_class_body(c, from_ast);
                }
            }
        }

        if (args && args->kind == AST_BLOCK) {
            c->current_class = klass;
            ObjFunction* method_fn = compile_block(c, args);
            c->current_class = NULL;
            add_method(klass, target_name, make_obj((Obj*)method_fn));

            if (from_class != NULL) {
                add_method_from(klass, target_name, from_class);
            }
            return;
        }
    
        TypeInfo* t_info = build_type_info(type_node);
        FieldInfo f_info = new_field_info(target_name, t_info, from_class, is_static);
    
        Value init_val = make_nil();
        if (args && args->kind == AST_LITERAL) {
            init_val = args->literal.value;
        } else {
            init_val = get_implicit_default_value(t_info ? t_info->name : NULL);
        }
    
        add_field(klass, f_info, init_val);
        return;
    }
}

void declare_class_skeleton(Compiler* c, ASTNode* node)
{
    if (node == NULL || node->kind != AST_CLASS_DEF) return;

    ASTNode* class_node = node->class_def.class;
    ObjString* class_name = class_node->class.classname->identifier.name;
    TypeInfo* class_type = build_type_info(class_node);

    ObjClass* klass = new_class(class_name, class_type, NULL, NULL, 0);
    
    if (c->defined_class == NULL) {
        c->defined_class = new_table(16);
    }
    table_set(c->defined_class, class_name, make_obj((Obj*)klass));
}

void compile_class_body(Compiler* c, ASTNode* node)
{
    ASTNode* class_node = node->class_def.class;
    ObjString* class_name = class_node->class.classname->identifier.name;

    ObjClass* klass = resolve_class(c, class_name);

    if (klass == NULL) {
        fprintf(stderr, "Fatal Error: Class '%s' not registered in skeleton pass.\n", class_name->chars);
        exit(EXIT_FAILURE);
    }

    // ★ 対策 1: すでにコンパイル完了していればスキップ (二重コンパイル防止)
    if (klass->state == CLASS_STATE_COMPILED) {
        return;
    }

    // ★ 対策 2: コンパイル中に再度自分に到達したら循環継承エラー
    if (klass->state == CLASS_STATE_COMPILING) {
        fprintf(stderr, "Error: Cyclic inheritance detected involving class '%s'.\n", class_name->chars);
        exit(EXIT_FAILURE);
    }

    // コンパイル中マークをセット
    klass->state = CLASS_STATE_COMPILING;

    ObjString* super_name = NULL;
    if (node->class_def.super_class != NULL) {
        super_name = get_class_name_from_node(node->class_def.super_class);
    } else {
        ObjString* obj_str = intern_cstr("Object");
        if (class_name != obj_str) {
            super_name = obj_str;
        } 
    }

    if (super_name != NULL) {
        klass->superclass = resolve_class(c, super_name);

        if (klass->superclass != NULL && klass->superclass->state != CLASS_STATE_COMPILED) {
            ASTNode* super_ast = find_class_ast(super_name);
            if (super_ast != NULL) {
                compile_class_body(c, super_ast);
            }
        }
    }

    int delegate_count = count_classes(node->class_def.from_classes);
    if (delegate_count > 0) {
        klass->delegates = (ObjClass**)malloc(sizeof(ObjClass*) * delegate_count);
        int idx = 0;
        collect_delegate_classes(c, node->class_def.from_classes, klass->delegates, &idx);
        klass->delegate_count = idx;
    }

    if (klass->superclass != NULL) {
        for (int i = 0; i < klass->superclass->field_count; i++) {
            add_field(klass, klass->superclass->field_infos[i], klass->superclass->default_values[i]);
        }
    }

    ObjClass* enclosing_class = c->current_class;
    c->current_class = klass;

    compile_class_members(c, klass, node->class_def.members);

    c->current_class = enclosing_class;

    klass->state = CLASS_STATE_COMPILED;

#if DEBUG_MODE
    printf("=== Compiled Class: %s ===\n", class_name->chars);
    print_class(klass);
#endif

    uint32_t class_const_idx = add_constant(c->chunk, make_obj((Obj*)klass));
    uint32_t name_sym_idx = add_constant(c->chunk, make_obj((Obj*)class_name));

    emit_inst(c, OP_CONSTANT, class_const_idx);
    emit_inst(c, OP_SET_GLOBAL, name_sym_idx);
    emit_inst(c, OP_POP, 0);
}

void compile_class_def(Compiler* c, ASTNode* node) 
{
    if (node == NULL || node->kind != AST_CLASS_DEF) return;

    ASTNode* class_node = node->class_def.class;
    ObjString* class_name = class_node->class.classname->identifier.name;

    if (resolve_class(c, class_name) == NULL) {
        declare_class_skeleton(c, node);
    }

    compile_class_body(c, node);
}
