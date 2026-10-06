#ifndef ERROR_H
#define ERROR_H

#include <stdbool.h>

typedef enum {
    LANG_EN,
    LANG_JA
} OutputLang;

typedef struct location {
    const char* filename;
    int line;
    int column;
    int col_end;
} Location;

extern OutputLang current_lang;

typedef enum {
    // --- 構文エラー  ---
    ERR_SYNTAX_GENERIC,
    ERR_EXPECTED_SEMICOLON,
    ERR_UNEXPECTED_TOKEN,

    // --- コンパイルエラー  ---
    ERR_UNDEFINED_VAR,
    ERR_ASSIGN_UNDEFINED_VAR,
    ERR_UNDEFINED_VAR_OR_FIELD,
    ERR_COMPOUND_ASSIGN_TARGET,
    ERR_TOO_MANY_LOCALS,
    ERR_BREAK_OUTSIDE_LOOP,
    ERR_CONTINUE_OUTSIDE_LOOP,
    ERR_DELEGATE_CLASS_NOT_FOUND,
    ERR_DELEGATE_METHOD_NOT_FOUND,
    ERR_METHOD_NO_IMPL,
    ERR_CLASS_NOT_REGISTERED,
    ERR_CYCLIC_INHERITANCE,
    ERR_UNHANDLED_AST_KIND,

    // --- 実行時エラー ---
    ERR_UNDEFINED_GLOBAL,
    ERR_CANNOT_INSTANTIATE,
    ERR_TYPE_OP_ADD,
    ERR_TYPE_OP_SUB,
    ERR_TYPE_OP_MUL,
    ERR_TYPE_OP_DIV,
    ERR_DIVISION_BY_ZERO,
    ERR_TYPE_OP_LESS,
    ERR_TYPE_OP_GREAT,
    ERR_FELL_OFF_END,
    ERR_UNKNOWN_OPCODE,
    ERR_UNDEFINED_CLASS_METHOD,
    ERR_WRONG_ARG_COUNT,
    ERR_STACK_OVERFLOW,
    ERR_UNDEFINED_MESSAGE,
    ERR_INDEX_OUT_OF_BOUNDS,
    ERR_PRINTLN_ARG_COUNT,

    ERR_COUNT
} ErrorCode;

// パーサー構文エラー用 (直前行のセミコロン補正付き)
void error_syntax(const char* filename, int line, int col_start, int col_end, const char* bison_msg);

// コンパイルエラー用 (位置情報あり / なし)
void error_at(Location loc, ErrorCode code, ...);
void error_compile(ErrorCode code, ...);

// VM 実行時エラー用
void error_runtime(ErrorCode code, ...);

#endif
