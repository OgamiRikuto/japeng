%{
#include "defs.h"
#include "ast.h"
#include "object.h"
#include "literal.h"
#include "symbol.h"
#include "error.h"

extern int yylex();
const char* current_filename = "";
int syntax_error_count = 0;
%}
%locations
%error-verbose
%union {
    int int_val;
    float float_val;
    ObjString* symbol;
    ASTNode* node;
}
%token CLASS        "class"
%token BASED        "based"
%token FROM         "from"
%token ARG          "arg"
%token RET          "ret"
%token RETURN       "return"
%token BREAK        "break"
%token CONTINUE     "continue"
%token STATIC       "static"

%token COMMA        ","
%token DOT          "."
%token COLON        ":"
%token SEMICOLON    ";"
%token L_PAR        "("
%token R_PAR        ")"
%token L_BLACKET    "["
%token R_BLACKET    "]"
%token UNKNOWN

%token <symbol> SELF            "self"
%token <symbol> IDENTIFIER      "identifier"
%token <symbol> CLASS_NAME      "class name"
%token <symbol> SP_IDENTIFIER   "operator"
%token <symbol> STRING          "string"
%token <symbol> FRACTION        "fraction"
%token <int_val> INTEGER        "integer"
%token <int_val> BINARY
%token <int_val> HEX
%token <float_val> FLOAT        "float"

%type <node> program statement statement_list class_def message
%type <node> member member_list receiver return_stmt
%type <node> class_decl name_def class type_list class_list field_decl
%type <node> identifier_decl identifier_decl_list statement_list_opt
%type <node> expression primary block args rets expression_list_opt expression_list
%type <node> control_chain control_clause_list control_clause

%nonassoc PREC_EXPR
%left IDENTIFIER SP_IDENTIFIER
%left COMMA
%left DOT
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
    { 
        if ($1 == NULL) $$ = $2;
        else if ($2 == NULL) $$ = $1;
        else $$ = create_stmt_node($1, $2); 
    }
    ;

statement_list_opt :
    /* empty */
    { $$ = NULL; }
    | statement_list
    { $$ = $1; }
    ;


statement : 
    message SEMICOLON
    { $$ = $1; }
    | identifier_decl SEMICOLON
    { $$ = $1; }
    | return_stmt SEMICOLON
    { $$ = $1; }
    | BREAK SEMICOLON
    { $$ = create_break_node(); }
    | CONTINUE SEMICOLON
    { $$ = create_continue_node(); }
    | error SEMICOLON
    { $$ = NULL; }
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
    | control_chain
    { $$ = $1; }
    | message DOT IDENTIFIER
    {
        ASTNode* msg = create_identifier_node($3);
        $$ = create_send_node($1, msg, NULL);
    }
    | message DOT IDENTIFIER expression_list
    {
        ASTNode* msg = create_identifier_node($3);
        $$ = create_send_node($1, msg, $4);
    }
    | message DOT SP_IDENTIFIER expression_list
    {
        ASTNode* msg = create_identifier_node($3);
        $$ = create_send_node($1, msg, $4);
    }
    ;

control_chain :
    IDENTIFIER L_PAR expression_list_opt R_PAR block control_clause_list
    {
        ASTNode* self_node = create_identifier_node(intern_cstr("self"));
        ASTNode* msg = create_identifier_node($1);
        ASTNode* args = ($3 == NULL) ? $5 : create_stmt_node($3, $5);
        ASTNode* first_clause = create_send_node(self_node, msg, args);

        if ($6 != NULL) {
            $$ = create_stmt_node(first_clause, $6);
        } else {
            $$ = first_clause;
        }
    }
    | IDENTIFIER block control_clause_list
    {
        ASTNode* self_node = create_identifier_node(intern_cstr("self"));
        ASTNode* msg = create_identifier_node($1);
        ASTNode* first_clause = create_send_node(self_node, msg, $2);
        if ($3 != NULL) {
            $$ = create_stmt_node(first_clause, $3);
        } else {
            $$ = first_clause;
        }
    }
    ;

control_clause_list :
    /* empty */
    { $$ = NULL;}
    | control_clause_list control_clause
    { $$ = ($1 == NULL) ? $2 : create_stmt_node($1, $2); }
    ;

control_clause :
    IDENTIFIER L_PAR expression_list_opt R_PAR block
    {
        ASTNode* msg = create_identifier_node($1);
        ASTNode* args = ($3 == NULL) ? $5 : create_stmt_node($3, $5);
        $$ = create_send_node(NULL, msg, args);
    }
    | IDENTIFIER block
    {
        ASTNode* msg = create_identifier_node($1);
        $$ = create_send_node(NULL, msg, $2);
    }
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
    field_decl SEMICOLON
    { $$ = $1; }
    | STATIC field_decl SEMICOLON
    { 
        $2->field_decl.is_static = true; 
        $$ = $2;
    }
    | message SEMICOLON
    { $$ = $1; }
    | STATIC message SEMICOLON
    {
        if($2->kind == AST_FIELD_DECL) {
            $2->field_decl.is_static = true;
        }else if ($2->kind == AST_SEND && $2->send.receiver && $2->send.receiver->kind == AST_VAR_DECL) {
            $2->send.receiver->var_decl.is_static = true;
        }
        $$ = $2;
    }
    | message FROM CLASS_NAME SEMICOLON
    {
        if ($1->kind == AST_SEND && $1->send.receiver && $1->send.receiver->kind == AST_VAR_DECL) {
            ASTNode* from_node = create_identifier_node($3);
            $$ = create_fielddecl_node(false, $1->send.receiver->var_decl.identifier, $1->send.receiver->var_decl.type, from_node, $1);
        } else {
            $$ = $1;
        }
    }
    | STATIC message FROM CLASS_NAME SEMICOLON
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
    | message %prec PREC_EXPR  { $$ = $1; }
    ;

primary : 
    IDENTIFIER  
    { 
        $$ = create_identifier_node($1); 
        $$->loc.column = @1.first_column;
    }
    | SELF      { $$ = create_identifier_node($1); }
    | INTEGER   { $$ = create_literal_node(make_int($1)); }
    | FLOAT     { $$ = create_literal_node(make_float($1)); }
    | FRACTION  { $$ = create_literal_node(make_obj($1)); }
    | BINARY    { }
    | HEX       { }
    | STRING    { $$ = create_literal_node(make_obj($1)); }
    | block     { $$ = $1; }
    | L_PAR expression R_PAR { $$ = $2; }
    | SP_IDENTIFIER primary
    {
        if ($1 == sym_minus) {
            ASTNode* zero = create_literal_node(make_int(0));
            ASTNode* op   = create_identifier_node($1);
            $$ = create_send_node(zero, op, $2);
        } else {
            // 将来の '!' や '~' などの単項演算子拡張用
            char buf[128];
            snprintf(buf, sizeof(buf), "unsupported unary operator '%s'", $1->chars);
            yyerror(buf);
        }
    }
    ;

block : 
    L_BLACKET statement_list_opt R_BLACKET 
    { $$ = create_block_node(NULL, NULL, $2); }
    | L_BLACKET args SEMICOLON statement_list_opt R_BLACKET 
    { $$ = create_block_node($2, NULL, $4); }
    | L_BLACKET rets SEMICOLON statement_list_opt R_BLACKET 
    { $$ = create_block_node(NULL, $2, $4); }
    | L_BLACKET args SEMICOLON rets SEMICOLON statement_list_opt R_BLACKET
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

void yyerror(const char *s) {
    syntax_error_count++;
    const char* fn = (current_filename && current_filename[0] != '\0') ? current_filename : "input";
    error_syntax(fn, yylloc.first_line, yylloc.first_column, yylloc.last_column, s);

    if (syntax_error_count >= 100) {
        fprintf(stderr, "fatal: too many errors emitted, stopping now.\n");
        exit(EXIT_FAILURE);
    }
}
