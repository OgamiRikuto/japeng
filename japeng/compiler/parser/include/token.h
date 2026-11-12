#ifndef TOKEN_H
#define TOKEN_H

#include "language.h"

typedef enum {
    TOK_EOF = 0,

    // 予約語
    CLASS = 258,
    INTERFACE,
    BASED,
    FROM,
    IMPLEMENTS,
    ARG,
    RET,
    RETURN,
    BREAK,
    CONTINUE,
    STATIC,
    IMPORT,
    AS,
    ELLIPSIS,

    // 記号
    COMMA,
    DOT,
    COLON,
    SEMICOLON,
    L_PAR,
    R_PAR,
    L_BLACKET,
    R_BLACKET,
    L_LIST_BLACKET,
    UNKNOWN,

    // 識別子・リテラル
    SELF,
    IDENTIFIER,
    CLASS_NAME,
    SP_IDENTIFIER,
    ASSIGN_IDENTIFIER,
    STRING,
    FRACTION,
    INTEGER,
    FLOAT,
    BINARY,
    HEX,
    TRUE_LITERAL,
    FALSE_LITERAL,
    NULL_LITERAL,

    // 文字列補間
    STR_OPEN,
    STR_CLOSE,
    STR_EXPR_OPEN,
    STR_EXPR_CLOSE,
    STR_TEXT,

    TOKEN_COUNT
} TokenType;

const char* parser_get_token_name(TokenType type, OutputLang lang);

#endif // TOKEN_H
