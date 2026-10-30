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
extern OutputLang compile_lang;

typedef enum {
    // --- 構文エラー  ---
    ERR_SYNTAX_GENERIC,
    ERR_EXPECTED_SEMICOLON,
    ERR_UNEXPECTED_TOKEN,
    ERR_SYNTAX_UNEXPECTED,
    ERR_SYNTAX_UNEXPECTED_EXPECTING,

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
    ERR_FIELD_INIT_MUST_BE_LITERAL,
    ERR_UNHANDLED_AST_KIND,
    ERR_FLOAT_OVERFLOW,
    ERR_TYPE_MISMATCH,
    ERR_ASSIGN_TYPE_MISMATCH,
    ERR_ARG_TYPE_MISMATCH,
    ERR_ARITY_MISMATCH,
    ERR_RETURN_TYPE_MISMATCH,
    ERR_NON_NUMERIC_OPERATION,          // 
    ERR_DEFAULT_ARG_ORDER,              // デフォルト引数右順違反
    ERR_ARITY_RANGE_MISMATCH,           // 引数不足or過多
    ERR_ARG_INIT_MUST_BE_LITERAL,       // 
    ERR_FUNCTION_REQUIRES_SIGNATURE,    // 関数変数宣言のシグネチャ不足
    ERR_BUILTIN_CACHE,                  // ビルトインクラスの不整合
    ERR_IF_COND_NOT_BOOL,               // if / elif 条件式
    ERR_REPEAT_COND_NOT_BOOL,           // repeat 条件ループ
    ERR_REPEAT_LIMIT_NOT_INT,           // repeat 回数ループ
    ERR_EACH_NOT_LIST,                  // each 対象が List 以外
    ERR_EACH_ARITY,                     // each のブロック引数個数不正
    ERR_EACH_INDEX_TYPE,                // each の添字引数型不正
    ERR_EACH_ELEMENT_TYPE,              // each の要素引数型不正
    ERR_INDEX_KEY_NOT_INT,              // 添字 access [i] の i が整数以外
    ERR_INDEX_TARGET_INVALID,           // 添字 access target[i] の対象不正
    ERR_BITWISE_OP_TYPE,                // ビット演算に整数以外
    ERR_LOGICAL_OP_TYPE,                // 論理演算 (and, or, !) に真偽値以外
    ERR_MULTI_ASSIGN_COUNT_MISMATCH,    // 複数代入の個数不一致
    ERR_BINARY_OP_TYPE_MISMATCH,        // 二項演算 (算術・比較) の両辺の型不一致
    ERR_METHOD_ARITY_MISMATCH,          // メソッドの引数個数不一致
    ERR_METHOD_ARITY_RANGE,             // メソッドの引数個数範囲不一致 (デフォルト引数)
    ERR_METHOD_ARG_TYPE_MISMATCH,       // メソッドの引数型不一致
    ERR_FUNC_ARITY_MISMATCH,            // 関数の引数個数不一致
    ERR_FUNC_ARG_TYPE_MISMATCH,         // 関数の引数型不一致
    ERR_RETURN_EMPTY_IN_VALUE_FUNC,     // 戻り値が必要なのに値が空の return
    ERR_RETURN_VALUE_IN_VOID_FUNC,      // Void 関数なのに値を返した return
    ERR_CASE_TYPE_MISMATCH,             // switch-case の条件式の型不一致

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
    ERR_CANNOT_ACCESS_FIELD_ON_CLASS,
    ERR_NULL_REFERENCE,
    ERR_INVALID_VTABLE_ACCESS,
    ERR_UNIMPLEMENTED_VIRTUAL_METHOD,
    ERR_NOT_CALLABLE,

    ERR_COUNT
} ErrorCode;

extern bool had_compile_error;
extern int  compile_error_count;

// パーサー構文エラー用 (直前行のセミコロン補正付き)
void error_syntax(const char* filename, int line, int col_start, int col_end, const char* bison_msg);

// コンパイルエラー用 (位置情報あり / なし)
void error_at(Location loc, ErrorCode code, ...);
void error_compile(ErrorCode code, ...);

// VM 実行時エラー用
void error_runtime(ErrorCode code, ...);

void report_undefined_message(const char* type_name, const char* message_name);

#endif
