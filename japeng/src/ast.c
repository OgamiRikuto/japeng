#include "ast.h"
#include <stdio.h>

extern int yylineno;
extern const char* current_filename;

static ASTNode* alloc_node(ASTNodeType kind)
{
    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
    if(node == NULL) {
        fprintf(stderr, "[Fatal Error] Out of memory during AST construction.\n");
        exit(EXIT_FAILURE);
    }

    node->kind = kind;
    node->line = yylineno;
    node->filename = current_filename ? current_filename : "unknown.je";

    return node;
}

ASTNode* create_stmt_node(ASTNode* left, ASTNode* right)
{
    ASTNode* node = alloc_node(AST_STMT);
    node->stmt.left = left;
    node->stmt.right = right;
    return node;
}

ASTNode* create_class_node(ASTNode* name, ASTNode* type)
{
    ASTNode* node = alloc_node(AST_CLASS);
    node->class.classname = name;
    node->class.types = type;
    return node;
}

ASTNode* create_literal_node(Value val)
{
    ASTNode* node = alloc_node(AST_LITERAL);
    node->literal.value = val;
    return node;
}

ASTNode* create_identifier_node(const char* name)
{
    ASTNode* node = alloc_node(AST_IDENTIFIER);
    node->identifier.name = name;
    return node;
}

ASTNode* create_send_node(ASTNode* receiver, ASTNode* message, ASTNode* args)
{
    ASTNode* node = alloc_node(AST_SEND);
    node->send.receiver = receiver;
    node->send.message = message;
    node->send.args = args;
    return node;
}

ASTNode* create_block_node(ASTNode* args, ASTNode* rets, ASTNode* body)
{
    ASTNode* node = alloc_node(AST_BLOCK);
    node->block.args = args;
    node->block.rets = rets;
    node->block.body = body;
    node->block.has_ret = rets != NULL;
    return node;
}

ASTNode* create_vardecl_node(ASTNode* identifier, ASTNode* type)
{
    ASTNode* node = alloc_node(AST_VAR_DECL);
    node->var_decl.identifier = identifier;
    node->var_decl.type = type;
    return node;
}

ASTNode* create_classdef_node(ASTNode* class, ASTNode* super, ASTNode* from, ASTNode* members)
{
    ASTNode* node = alloc_node(AST_CLASS_DEF);
    node->class_def.class = class;
    node->class_def.super_class = super;
    node->class_def.from_classes = from;
    node->class_def.members = members;
    return node;
}

ASTNode* create_fielddecl_node(bool is_static, ASTNode* identifier, ASTNode* type, ASTNode* from, ASTNode* default_data)
{
    ASTNode* node = alloc_node(AST_FIELD_DECL);
    node->field_decl.is_static = is_static;
    node->field_decl.identifier = identifier;
    node->field_decl.type = type;
    node->field_decl.from_class = from;
    node->field_decl.default_data = default_data;
    return node;
}

ASTNode* create_return_node(ASTNode* expr)
{
    ASTNode* node = alloc_node(AST_RETURN);
    node->ret.expr = expr;
    return node;
}

ASTNode* create_break_node()
{
    return alloc_node(AST_BREAK);
}
ASTNode* create_continue_node()
{
    return alloc_node(AST_CONTINUE);
}
