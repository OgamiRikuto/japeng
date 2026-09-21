%{
#include "defs.h"
#include "ast.h"
#include "object.h"
#include "literal.h"
#include "symbol.h"

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
%token SELF         "self"

%token COMMA        ","
%token PERIOD       "."
%token COLON        ":"
%token L_PAR        "("
%token R_PAR        ")"
%token L_BLACKET    "["
%token R_BLACKET    "]"
%token UNKNOWN

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
    | error PERIOD
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
    | SP_IDENTIFIER primary
    {
        if (strcmp($1->chars, "-") == 0) {
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
// 行が空行（空白・改行のみ）または単一行コメントかどうかを判定
static int is_ignorable_line(const char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    if (*s == '\0') return 1; // 空行
    if (s[0] == '/' && s[1] == '/') return 1; // コメント行
    return 0;
}

// ファイルから直前の有効なコード行（空行・コメント以外）を探し出して表示する
static int print_previous_valid_line(const char* filename, int current_err_line) {
    FILE* fp = fopen(filename, "r");
    if (!fp) return 0;

    char lines[256][1024];
    int line_count = 0;

    while (line_count < 256 && fgets(lines[line_count], sizeof(lines[line_count]), fp)) {
        line_count++;
    }
    fclose(fp);

    // エラー発生行（例: 18行目）の手前から上に向かってスキャン
    for (int l = current_err_line - 1; l >= 1; l--) {
        if (l <= line_count && !is_ignorable_line(lines[l - 1])) {
            char* buf = lines[l - 1];
            buf[strcspn(buf, "\r\n")] = '\0';

            // 行末の非空白文字の位置（ピリオドを打つべき場所）を特定
            int end_col = strlen(buf);
            while (end_col > 0 && (buf[end_col - 1] == ' ' || buf[end_col - 1] == '\t')) {
                end_col--;
            }
            int caret_col = end_col + 1;

            // エラーヘッダーの出力
            fprintf(stderr, "\033[1m%s:%d:%d: \033[1;31merror:\033[0m\033[1m syntax error, expected '.' at end of statement\033[0m\n",
                    filename, l, caret_col);

            // ソースコード行の出力
            fprintf(stderr, "%6d | %s\n", l, buf);
            fprintf(stderr, "       | ");
            for (int i = 1; i < caret_col; i++) {
                fputc((buf[i - 1] == '\t') ? '\t' : ' ', stderr);
            }
            fprintf(stderr, "\033[1;32m^\033[0m\n\n");
            return 1; // 補正成功
        }
    }
    return 0;
}

static void print_error_snippet(const char* filename, int line, int col_start, int col_end) {
    if (!filename || filename[0] == '\0') return;
    FILE* fp = fopen(filename, "r");
    if (!fp) return;

    char buf[1024];
    int cur_line = 1;

    while (fgets(buf, sizeof(buf), fp)) {
        if (cur_line == line) {
            buf[strcspn(buf, "\r\n")] = '\0';
            fprintf(stderr, "%6d | %s\n", line, buf);
            fprintf(stderr, "       | ");

            int len = strlen(buf);
            for (int i = 1; i < col_start; i++) {
                char ch = (i - 1 < len) ? buf[i - 1] : ' ';
                fputc((ch == '\t') ? '\t' : ' ', stderr);
            }

            int width = (col_end >= col_start) ? (col_end - col_start + 1) : 1;
            fprintf(stderr, "\033[1;32m^");
            for (int i = 1; i < width; i++) {
                fputc('~', stderr);
            }
            fprintf(stderr, "\033[0m\n");
            break;
        }
        cur_line++;
    }
    fclose(fp);
}

void yyerror(const char *s) {
    syntax_error_count++;

    const char* fn = (current_filename && current_filename[0] != '\0') ? current_filename : "input";

    // ピリオド欠落（expecting .）によるエラーで、行頭付近で破綻した場合
    // 上に向かって直前の有効なコード行を探して表示
    if (strstr(s, "expecting .") && print_previous_valid_line(fn, yylloc.first_line)) {
        // 直前行のピリオド抜けとして補正表示できた場合はカスケードを防ぐためここで終了
        exit(EXIT_FAILURE);
    }

    // 通常の構文エラー表示
    fprintf(stderr, "\033[1m%s:%d:%d: \033[1;31merror:\033[0m\033[1m %s\033[0m\n",
            fn, yylloc.first_line, yylloc.first_column, s);

    print_error_snippet(fn, yylloc.first_line, yylloc.first_column, yylloc.last_column);
    fprintf(stderr, "\n");

    if (syntax_error_count >= 100) {
        fprintf(stderr, "fatal: too many errors emitted, stopping now.\n");
        exit(EXIT_FAILURE);
    }
}
