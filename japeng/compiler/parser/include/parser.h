#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>
#include "ast.h"
#include "symbol.h"
#include "compiler-error.h"

// トークン構造体 (字句解析器 Flex から受け取る単位)
typedef struct {
    int       type;         // TOKEN_IDENTIFIER, TOKEN_COLON など
    Symbol*   symbol;       // 識別子、クラス名、文字列の値
    int       int_val;      // 整数値
    double    float_val;    // 浮動小数点数値
    Location  loc;          // ソース位置 (line, column)
} Token;

// 動的トークン配列
typedef struct {
    Token* tokens;
    int    count;
    int    capacity;
} TokenArray;

// パーサー状態構造体
typedef struct {
    Token*      tokens;     // トークン配列への参照
    int         count;      // トークン総数
    int         pos;        // 現在着目しているトークンのインデックス
    const char* filename;   // ソースファイル名 (エラー表示用)
    bool        had_error;  // パースエラー発生フラグ
    bool        panic_mode; // カスケードエラー抑止用フラグ
} Parser;

// --- 基本操作 API ---
void   parser_init(Parser* p, Token* tokens, int count, const char* filename);
Token  peek(Parser* p, int distance);
int    peek_type(Parser* p, int distance);
Token  advance(Parser* p);
Token  previous(Parser* p);
bool   check(Parser* p, int expected_type);
bool   match(Parser* p, int expected_type);
Token  consume(Parser* p, int expected_type);
void   synchronize(Parser* p);

// --- 動的トークン配列操作 ---
void token_array_init(TokenArray* arr);
void token_array_push(TokenArray* arr, Token tok);
void token_array_free(TokenArray* arr);

ASTNode* parse_program(Parser* p);
ASTNode* parse_import_stmt_list(Parser* p);
ASTNode* parse_import_stmt(Parser* p);
ASTNode* parse_type_def_list(Parser* p);
ASTNode* parse_type_def(Parser* p);
ASTNode* parse_statement_list(Parser* p);
ASTNode* parse_class_def(Parser* p);
ASTNode* parse_class_decl(Parser* p);
ASTNode* parse_interface_def(Parser* p);
ASTNode* parse_interface_decl(Parser* p);
ASTNode* parse_member_list(Parser* p);
ASTNode* parse_member(Parser* p);
ASTNode* parse_type(Parser* p);
ASTNode* parse_type_arg_list(Parser* p);
ASTNode* parse_type_list_comma(Parser* p);
ASTNode* parse_type_list_commma_opt(Parser* p);
ASTNode* parse_statement(Parser* p);
ASTNode* parse_identifier_decl(Parser* p);
ASTNode* parse_multi_identifier(Parser* p);
ASTNode* parse_expression(Parser* p);
ASTNode* parse_receiver(Parser* p);
ASTNode* parse_message(Parser* p);
ASTNode* parse_control_chain(Parser* p);
ASTNode* parse_control_clause(Parser* p);
ASTNode* parse_primary(Parser* p);
ASTNode* parse_literal(Parser* p);
ASTNode* parse_interpolated_string(Parser* p);
ASTNode* parse_block(Parser* p);
ASTNode* parse_arg_decl_list(Parser* p);
ASTNode* parse_arg_decl(Parser* p);
ASTNode* parse_expr_list(Parser* p);
ASTNode* parse_statement_list_opt(Parser* p);

#endif // PARSER_H
