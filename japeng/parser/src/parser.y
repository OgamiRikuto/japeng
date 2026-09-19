%{
#include "defs.h"
#include "ast.h"
#include "literal.h"
#include "symbol.h"

extern int yylex();
%}
%union {
    int int_val;
    float float_val;
    ObjString* symbol;
    ASTNode* node;
}
%token CLASS BASED FROM ARG RET RETURN BREAK CONTINUE STATIC SELF
%token COMMA PERIOD COLON L_PAR R_PAR L_BLACKET R_BLACKET UNKNOWN
%token <symbol> IDENTIFIER CLASS_NAME SP_IDENTIFIER STRING FRACTION
%token <int_val> INTEGER BINARY HEX
%token <float_val> FLOAT

%type <node> program statement statement_list class_def message
%type <node> member member_list receiver return_stmt
%type <node> class_decl name_def class type_list class_list field_decl
%type <node> identifier_decl identifier_decl_list statement_list_opt
%type <node> expression primary block args rets expression_list_opt expression_list
%left IDENTIFIER SP_IDENTIFIER
%left COMMA
%%
program : 
    statement_list
    { $$ = $1; 
      current_parsed_ast = $$;
    }
    | class_def
    { $$ = $1; 
      current_parsed_ast = $$;
    }
    ;

statement_list : 
    statement 
    { $$ = $1; }
    | statement_list statement
    { $$ = create_stmt_node($1, $2); }
    ;

statement_list_opt :
    /* empty */
    { $$ = NULL; }
    | statement_list
    { $$ = $1; }
    ;


statement : 
    message PERIOD
    { $$ = $1; }
    | identifier_decl PERIOD
    { $$ = $1; }
    | return_stmt PERIOD 
    { $$ = $1; }
    | BREAK PERIOD
    { $$ = create_break_node(); }
    | CONTINUE PERIOD
    { $$ = create_continue_node(); }
    ;

class_def : 
    class_decl COLON member_list
    {
        $1->class_def.members = $3;
        $$ = $1;
    }
    ;

message : 
    receiver IDENTIFIER 
    {
        ASTNode* msg = create_identifier_node($2);
        $$ = create_send_node($1, msg, NULL);
    }
    | receiver IDENTIFIER expression_list 
    {
        ASTNode* msg = create_identifier_node($2);
        $$ = create_send_node($1, msg, $3);
    }
    | receiver SP_IDENTIFIER expression_list 
    {
        ASTNode* msg = create_identifier_node($2);
        $$ = create_send_node($1, msg, $3);
    }
    | IDENTIFIER L_PAR expression_list_opt R_PAR
    {
        ASTNode* self_node = create_identifier_node(intern_cstr("self"));
        ASTNode* msg = create_identifier_node($1);
        $$ = create_send_node(self_node, msg, $3);
    }

    /* 2. 制御構文スタイル: if (cond) [ body ] や repeat (cond) [ body ] */
    | IDENTIFIER L_PAR expression_list_opt R_PAR block
    {
        ASTNode* self_node = create_identifier_node(intern_cstr("self"));
        ASTNode* msg = create_identifier_node($1);
        // 条件式 ($3) と ブロック ($5) を引数チェインとして結合
        ASTNode* args = ($3 == NULL) ? $5 : create_stmt_node($3, $5);
        $$ = create_send_node(self_node, msg, args);
    }

    /* 3. 引数なしブロックスタイル: repeat [ body ] (無限ループ) */
    | IDENTIFIER block
    {
        ASTNode* self_node = create_identifier_node(intern_cstr("self"));
        ASTNode* msg = create_identifier_node($1);
        $$ = create_send_node(self_node, msg, $2);
    }
    ;
    ;


class_decl : 
    name_def 
    { $$ = create_classdef_node($1, NULL, NULL, NULL); }
    | name_def BASED class 
    { $$ = create_classdef_node($1, $3, NULL, NULL); }
    | name_def FROM class_list
    { $$ = create_classdef_node($1, NULL, $3, NULL); }
    ;

name_def : 
    CLASS class { $$ = $2; }
    ;

class : 
    CLASS_NAME 
    { 
        ASTNode* name = create_identifier_node($1); 
        $$ = create_class_node(name, NULL);
    }
    | CLASS_NAME type_list
    {
        ASTNode* name = create_identifier_node($1); 
        $$ = create_class_node(name, $2);
    }
    ;

type_list : 
    CLASS_NAME 
    {
        ASTNode* name = create_identifier_node($1); 
        $$ = create_class_node(name, NULL);
    }
    | type_list CLASS_NAME
    {
        ASTNode* name = create_identifier_node($2);
        ASTNode* type = create_class_node(name, NULL); 
        $$ = create_stmt_node($1, type);
    }
    ;

class_list : 
    class { $$ = $1; }
    | class_list COMMA class
    { $$ = create_stmt_node($1, $3); }
    ;

member_list : 
    member { $$ = $1; }
    | member_list member
    { $$ = create_stmt_node($1, $2); }
    ;

member : 
    field_decl PERIOD
    { $$ = $1; }
    | STATIC field_decl PERIOD
    { 
        $2->field_decl.is_static = true; 
        $$ = $2;
    }
    | message PERIOD
    { $$ = $1; }
    | STATIC message PERIOD
    {
        if($2->kind == AST_FIELD_DECL) {
            $2->field_decl.is_static = true;
        }else if ($2->kind == AST_SEND && $2->send.receiver && $2->send.receiver->kind == AST_VAR_DECL) {
            $2->send.receiver->var_decl.is_static = true;
        }
        $$ = $2;
    }
    | message FROM CLASS_NAME PERIOD
    {
        if ($1->kind == AST_SEND && $1->send.receiver && $1->send.receiver->kind == AST_VAR_DECL) {
            ASTNode* from_node = create_identifier_node($3);
            $$ = create_fielddecl_node(false, $1->send.receiver->var_decl.identifier, $1->send.receiver->var_decl.type, from_node, $1);
        } else {
            $$ = $1;
        }
    }
    | STATIC message FROM CLASS_NAME PERIOD
    {
        
        if ($2->kind == AST_SEND && $2->send.receiver && $2->send.receiver->kind == AST_VAR_DECL) {
            ASTNode* from_node = create_identifier_node($4);
            $$ = create_fielddecl_node(true, $2->send.receiver->var_decl.identifier, $2->send.receiver->var_decl.type, from_node, $2);
        } else {
            $$ = $2;
        }
    }
    ;

field_decl : 
    identifier_decl 
    { $$ = create_fielddecl_node(false, $1->var_decl.identifier, $1->var_decl.type, NULL, NULL); }
    | identifier_decl FROM CLASS_NAME
    { 
        ASTNode* from = create_identifier_node($3);
        $$ = create_fielddecl_node(false, $1->var_decl.identifier, $1->var_decl.type, from, NULL); 
    }
    ;

identifier_decl : 
    IDENTIFIER COLON class
    { $$ = create_vardecl_node(create_identifier_node($1), $3); }
    | SP_IDENTIFIER COLON class
    { $$ = create_vardecl_node(create_identifier_node($1), $3);}
    ;

expression : 
    primary     { $$ = $1; }
    | message   { $$ = $1; }
    ;

primary : 
    IDENTIFIER  { $$ = create_identifier_node($1); }
    | INTEGER   { $$ = create_literal_node(make_int($1)); }
    | FLOAT     { $$ = create_literal_node(make_float($1)); }
    | FRACTION  { $$ = create_literal_node(make_obj($1)); }
    | BINARY    { }
    | HEX       { }
    | STRING    { $$ = create_literal_node(make_obj($1)); }
    | block     { $$ = $1; }
    | L_PAR expression R_PAR { $$ = $2; }
    ;

block : 
    L_BLACKET statement_list_opt R_BLACKET 
    { $$ = create_block_node(NULL, NULL, $2); }
    | L_BLACKET args PERIOD statement_list_opt R_BLACKET 
    { $$ = create_block_node($2, NULL, $4); }
    | L_BLACKET rets PERIOD statement_list_opt R_BLACKET 
    { $$ = create_block_node(NULL, $2, $4); }
    | L_BLACKET args PERIOD rets PERIOD statement_list_opt R_BLACKET
    { $$ = create_block_node($2, $4, $6); }
    ;

args : 
    ARG identifier_decl_list
    { $$ = $2; }
    ;

rets : 
    RET type_list
    { $$ = $2; }
    ;

identifier_decl_list : 
    identifier_decl 
    { $$ = $1; }
    | identifier_decl_list COMMA identifier_decl
    { $$ = create_stmt_node($1, $3); }
    ;

receiver : 
    primary 
    { $$ = $1; }
    | identifier_decl
    { $$ = $1; }
    | CLASS_NAME
    { $$ = create_identifier_node($1); }
    | SELF
    { $$ = create_identifier_node(intern_cstr("self")); }
    ;

expression_list_opt : 
    /* empty */
    { $$ = NULL; }
    | expression_list
    { $$ = $1; }
    ;

expression_list :   
    expression  { $$ = $1; }
    | expression_list COMMA expression  
    { $$ = create_stmt_node($1, $3); }
    ; 

return_stmt : 
    RETURN expression_list
    { $$ = create_return_node($2); }
    ;

%%
#include "lex.yy.c"
void yyerror(char *s) {
	fprintf(stderr, "\n%s at %d: nearby \"%s\"\n\n", s, linecounter, yytext);
	exit(EXIT_FAILURE);
}
