%{
#include "defs.h"
#include "ast.h"
#include "literal.h"

extern int yylex();
%}
%union {
    int int_val;
    float float_val;
    char* string;
    ASTNode* node;
}
%token CLASS BASED FROM ARG RET RETURN BREAK CONTINUE STATIC SELF
%token COMMA PERIOD COLON L_PAR R_PAR L_BLACKET R_BLACKET UNKNOWN
%token <string> IDENTIFIER CLASS_NAME SP_IDENTIFIER STRING FRACTION
%token <int_val> INTEGER BINARY HEX
%token <float_val> FLOAT

%type <node> program statement statement_list class_def message
%type <node> member member_list receiver message_send return_stmt
%type <node> class_decl name_def class type_list class_list field_decl
%type <node> identifier_decl identifier_decl_list decl_name
%type <node> expression primary block args rets expression_list
%%
program : 
    statement_list
    { $$ = $1; 
      if(parsed_file_count < MAX_FILES) {
        parsed_files[parsed_file_count++] = $$;
      } else {
        fprintf(stderr, "Error: too many files parsed.\n");
      }
    }
    | class_def
    { $$ = $1; 
      if(parsed_file_count < MAX_FILES) {
        parsed_files[parsed_file_count++] = $$;
      } else {
        fprintf(stderr, "Error: too many files parsed.\n");
      }
    }
    ;

statement_list : 
    statement 
    { $$ = $1; }
    | statement_list statement
    { $$ = create_stmt_node($1, $2); }
    ;

statement : 
    message PERIOD
    { $$ = $1; }
    | return_stmt PERIOD 
    { $$ = $1; }
    | BREAK PERIOD
    { $$ = create_break_node(); }
    | CONTINUE PERIOD
    { $$ = create_continue_node(); }
    ;

class_def : 
    class_decl member_list
    {
        $1->class_def.members = $2;
        $$ = $1;
    }
    ;

message : 
    receiver message_send 
    {
        $2->send.receiver = $1;
        $$ = $2;
    }
    | message_send { $$ = $1; }
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
    | type_list COMMA CLASS_NAME
    {
        ASTNode* name = create_identifier_node($3);
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
        }
        $$ = $2;
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
    decl_name COLON class
    { $$ = create_vardecl_node($1, $3); }
    ;

decl_name :
    IDENTIFIER
    { $$ = create_identifier_node($1); }
    | SP_IDENTIFIER
    { $$ = create_identifier_node($1); }
    ;

expression : 
    primary     { $$ = $1; }
    | message   { $$ = $1; }
    ;

primary : 
    IDENTIFIER  { $$ = create_identifier_node($1); }
    | INTEGER   { $$ = create_literal_node(make_int($1)); }
    | FLOAT     { $$ = create_literal_node(make_float($1)); }
    | FRACTION  { }
    | BINARY    { }
    | HEX       { }
    | STRING    { }
    | block     { $$ = $1; }
    | L_PAR expression R_PAR { $$ = $2; }
    ;

block : 
    L_BLACKET statement_list R_BLACKET 
    { $$ = create_block_node(NULL, NULL, $2); }
    | L_BLACKET args PERIOD statement_list R_BLACKET 
    { $$ = create_block_node($2, NULL, $4); }
    | L_BLACKET rets PERIOD statement_list R_BLACKET 
    { $$ = create_block_node(NULL, $2, $4); }
    | L_BLACKET args PERIOD rets PERIOD statement_list R_BLACKET
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
    | L_PAR message R_PAR
    { $$ = $2; }
    | identifier_decl
    { $$ = $1; }
    | CLASS_NAME
    { $$ = create_identifier_node($1); }
    | SELF
    { $$ = create_identifier_node("self"); }
    ;

message_send : 
    IDENTIFIER 
    {
        ASTNode* msg = create_identifier_node($1);
        $$ = create_send_node(NULL, msg, NULL);
    }
    | IDENTIFIER expression_list 
    {
        ASTNode* msg = create_identifier_node($1);
        $$ = create_send_node(NULL, msg, $2);
    }
    | SP_IDENTIFIER expression 
    {
        ASTNode* msg = create_identifier_node($1);
        $$ = create_send_node(NULL, msg, $2);
    }
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
