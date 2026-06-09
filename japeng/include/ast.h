#ifndef __AST_H__
#define __AST_H__

#include <stdlib.h>
#include <string.h>
#include "literal.h"

typedef enum {
    AST_STMT,
    AST_CLASS,
    AST_LITERAL,
    AST_IDENTIFIER,
    AST_SEND,
    AST_BLOCK,
    AST_VAR_DECL,
    AST_FIELD_DECL,
    AST_CLASS_DEF,
    AST_RETURN,
    AST_BREAK,
    AST_CONTINUE
} ASTNodeType;

typedef struct ASTNode ASTNode;

struct ASTNode {
    ASTNodeType kind;
    int line;
    const char* filename;

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
            Value value;
        } literal;

        struct {
            const char* name;
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
            ASTNode* identifier;
            ASTNode* type;
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
    };
};

ASTNode* create_stmt_node(ASTNode*, ASTNode*);
ASTNode* create_class_node(ASTNode*, ASTNode*);
ASTNode* create_literal_node(Value);
ASTNode* create_identifier_node(const char*);
ASTNode* create_send_node(ASTNode*, ASTNode*, ASTNode*);
ASTNode* create_block_node(ASTNode*, ASTNode*, ASTNode*);
ASTNode* create_vardecl_node(ASTNode*, ASTNode*);
ASTNode* create_classdef_node(ASTNode*, ASTNode*, ASTNode*, ASTNode*);
ASTNode* create_fielddecl_node(bool, ASTNode*, ASTNode*, ASTNode*, ASTNode*);
ASTNode* create_return_node(ASTNode*);
ASTNode* create_break_node();
ASTNode* create_continue_node();


#endif
