#ifndef AST_H
#define AST_H

#include <stdlib.h>
#include <string.h>
#include "literal.h"
#include "symbol.h"
#include "error.h"

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
} ASTNodeType;

typedef enum {
    TOKEN_INTEGER,
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
        } class;

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
            ASTNode* class;
            ASTNode* super_class;
            ASTNode* from_classes;
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
    };
};

ASTNode* create_stmt_node(ASTNode*, ASTNode*);
ASTNode* create_class_node(ASTNode*, ASTNode*);
ASTNode* create_func_type_node(ASTNode*, ASTNode*);
ASTNode* create_literal_node(LiteralKind, Symbol*);
ASTNode* create_identifier_node(Symbol*);
ASTNode* create_send_node(ASTNode*, ASTNode*, ASTNode*);
ASTNode* create_block_node(ASTNode*, ASTNode*, ASTNode*);
ASTNode* create_vardecl_node(ASTNode*, ASTNode*);
ASTNode* create_classdef_node(ASTNode*, ASTNode*, ASTNode*, ASTNode*);
ASTNode* create_fielddecl_node(bool, ASTNode*, ASTNode*, ASTNode*, ASTNode*);
ASTNode* create_return_node(ASTNode*);
ASTNode* create_break_node();
ASTNode* create_continue_node();
ASTNode* create_index_access_node(ASTNode*, ASTNode*);

ASTNode* fix_precedence(ASTNode* node);
ASTNode* fold_constants(ASTNode* node);


#endif
