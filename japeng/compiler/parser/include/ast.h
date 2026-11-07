#ifndef AST_H
#define AST_H

#include <stdlib.h>
#include <string.h>
#include "literal.h"
#include "symbol.h"
#include "compiler-error.h"

typedef enum {
    AST_STMT,
    AST_CLASS,
    AST_FUNC_TYPE,
    AST_LITERAL,
    AST_IDENTIFIER,
    AST_SEND,
    AST_BLOCK,
    AST_VAR_DECL,
    AST_FIELD_DECL,
    AST_CLASS_DEF,
    AST_RETURN,
    AST_BREAK,
    AST_CONTINUE,
    AST_INDEX_ACCESS,
    AST_CONSTRUCT,
    AST_IMPORT,
} ASTNodeType;

typedef enum {
    TOKEN_INTEGER,
    TOKEN_BINARY,
    TOKEN_HEX,
    TOKEN_FLOAT,
    TOKEN_STRING,
    TOKEN_FRACTION,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_NULL,
    TOKEN_MAX
} LiteralKind;

typedef struct ASTNode ASTNode;

struct ASTNode {
    ASTNodeType kind;
    Location loc;

    union {
        struct {
            ASTNode* left;
            ASTNode* right;
        } stmt;

        struct {
            ASTNode* classname;
            ASTNode* types;
        } klass;

        struct {
            ASTNode* params;
            ASTNode* rets;
        } func_type;

        struct {
            LiteralKind kind;
            Symbol* raw;
        } literal;

        struct {
            Symbol* name;
        } identifier;

        struct {
            ASTNode* receiver;
            ASTNode* message;
            ASTNode* args;
        } send;

        struct {
            ASTNode* args;
            ASTNode* rets;
            bool has_ret;
            ASTNode* body;
        } block;

        struct {
            bool is_static;
            ASTNode* identifier;
            ASTNode* type;
            ASTNode* default_val;
        } var_decl;
        
        struct {
            bool is_interface;
            ASTNode* klass;
            ASTNode* super_class;
            ASTNode* from_classes;
            ASTNode* interfaces;
            ASTNode* members;
        } class_def;
        
        struct {
            bool is_static;
            ASTNode* identifier;
            ASTNode* type;
            ASTNode* from_class;
            ASTNode* default_data;
        } field_decl;

        struct {
            ASTNode* expr;
        } ret;

        struct {
            ASTNode* target;
            ASTNode* index;
        } index_access;

        struct {
            ASTNode* class_node;
            ASTNode* args;
        } construct;

        struct {
            Symbol* path;
            ASTNode* name;
            ASTNode* alias;
        } import_stmt;
    };
};

ASTNode* create_stmt_node(ASTNode* left, ASTNode* right);
ASTNode* create_class_node(ASTNode* classname, ASTNode* types);
ASTNode* create_func_type_node(ASTNode* params, ASTNode* rets);
ASTNode* create_literal_node(LiteralKind kind, Symbol* raw);
ASTNode* create_identifier_node(Symbol* name);
ASTNode* create_send_node(ASTNode* receiver, ASTNode* message, ASTNode* args);
ASTNode* create_block_node(ASTNode* args, ASTNode* rets, ASTNode* body);
ASTNode* create_vardecl_node(ASTNode* identifier, ASTNode* type);
ASTNode* create_classdef_node(bool is_interface, ASTNode* klass, ASTNode* super, ASTNode* from, ASTNode* iface, ASTNode* members);
ASTNode* create_fielddecl_node(bool is_static, ASTNode* identifier, ASTNode* type, ASTNode* from_class, ASTNode* default_data);
ASTNode* create_return_node(ASTNode* expr);
ASTNode* create_break_node();
ASTNode* create_continue_node();
ASTNode* create_index_access_node(ASTNode* target, ASTNode* index);
ASTNode* create_construct_node(ASTNode* class_node, ASTNode* args);
ASTNode* create_import_node(Symbol* path, ASTNode* name, ASTNode* alias);

ASTNode* fix_precedence(ASTNode* node);
ASTNode* fold_constants(ASTNode* node);


#endif
