#ifndef ERROR_H
#define ERROR_H

#include "language.h"
#include <stdbool.h>

typedef struct location {
    const char* filename;
    int line;
    int column;
    int col_end;
} Location;

// extern OutputLang current_lang;
// extern OutputLang compile_lang;

typedef enum {
    // --- システム・CLI・通知メッセージ ---
    MSG_FORMAT_OVERWRITTEN,          // 整形上書き完了
    ERR_SYS_CANNOT_OPEN_FILE,        // ファイルオープン失敗
    ERR_SYS_INVALID_SOURCE_FILE,     // 対象外のソースファイル
    ERR_SYS_TOO_MANY_FILES,          // ファイル数上限超過
    ERR_SYS_NO_TARGET_FILE,          // 対象ファイル未指定

    // --- 構文エラー ---
    ERR_SYNTAX_GENERIC,
    ERR_EXPECTED_SEMICOLON,
    ERR_UNEXPECTED_TOKEN,
    ERR_SYNTAX_UNEXPECTED,
    ERR_SYNTAX_UNEXPECTED_EXPECTING,

    // ★ 初心者救済用・詳細構文エラー
    ERR_SYNTAX_METHOD_CALL_PARENS_REQUIRED, // メソッド呼出のカッコ忘れ (foo x;)
    ERR_SYNTAX_UNCLOSED_BLOCK,              // ブロック '[' 閉じ忘れ
    ERR_SYNTAX_UNCLOSED_PAREN,              // 丸括弧 '(' 閉じ忘れ
    ERR_SYNTAX_MISSING_TYPE_COLON,          // 変数宣言のコロン忘れ (x Integer;)
    ERR_SYNTAX_INVALID_ASSIGN_OPERATOR,     // '=' を代入に使った (x = 10;)
    ERR_SYNTAX_LOWERCASE_CLASS_NAME,        // クラス名が小文字始まり
    ERR_SYNTAX_MISSING_CLASS_COLON,         // class 定義末尾の ':' 忘れ
    ERR_SYNTAX_INVALID_ARG_DECL,            // 引数定義の書式不正
    ERR_SYNTAX_UNEXPECTED_EOF,              // 途中でファイル終端に達した

    // ★ 多言語・モジュール辞書用エラー (将来拡張スロット)
    ERR_MODULE_NO_DICT_FOR_LANG,            // モジュールに指定言語の辞書が存在しない
    ERR_DICT_ENTRY_NOT_FOUND,               // 辞書内に単語が見つからない

    // --- コンパイルエラー ---
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
    ERR_NON_NUMERIC_OPERATION,
    ERR_DEFAULT_ARG_ORDER,
    ERR_ARITY_RANGE_MISMATCH,
    ERR_ARG_INIT_MUST_BE_LITERAL,
    ERR_FUNCTION_REQUIRES_SIGNATURE,
    ERR_BUILTIN_CACHE,
    ERR_IF_COND_NOT_BOOL,
    ERR_REPEAT_COND_NOT_BOOL,
    ERR_REPEAT_LIMIT_NOT_INT,
    ERR_EACH_NOT_LIST,
    ERR_EACH_ARITY,
    ERR_EACH_INDEX_TYPE,
    ERR_EACH_ELEMENT_TYPE,
    ERR_INDEX_KEY_NOT_INT,
    ERR_INDEX_TARGET_INVALID,
    ERR_BITWISE_OP_TYPE,
    ERR_LOGICAL_OP_TYPE,
    ERR_MULTI_ASSIGN_COUNT_MISMATCH,
    ERR_BINARY_OP_TYPE_MISMATCH,
    ERR_METHOD_ARITY_MISMATCH,
    ERR_METHOD_ARITY_RANGE,
    ERR_METHOD_ARG_TYPE_MISMATCH,
    ERR_FUNC_ARITY_MISMATCH,
    ERR_FUNC_ARG_TYPE_MISMATCH,
    ERR_RETURN_EMPTY_IN_VALUE_FUNC,
    ERR_RETURN_VALUE_IN_VOID_FUNC,
    ERR_CASE_TYPE_MISMATCH,
    ERR_INDEX_ASSIGN_TYPE_MISMATCH,
    ERR_INTERFACE_CANNOT_HAVE_FIELDS,
    ERR_INTERFACE_METHOD_CANNOT_HAVE_BODY,
    ERR_INTERFACE_NOT_IMPLEMENTED,
    ERR_INTERFACE_METHOD_CONFLICT,
    ERR_CANNOT_INSTANTIATE_INTERFACE,
    ERR_UNDEFINED_MESSAGE,
    ERR_WITH_REQUIRES_CLOSE,        // Close インターフェース未実装
    ERR_WITH_SYNTAX,                // 式またはブロックの不足
    ERR_MODULE_NOT_FOUND,
    

    // // --- 実行時エラー ---
    // ERR_UNDEFINED_GLOBAL,
    // ERR_CANNOT_INSTANTIATE,
    // ERR_TYPE_OP_ADD,
    // ERR_TYPE_OP_SUB,
    // ERR_TYPE_OP_MUL,
    // ERR_TYPE_OP_DIV,
    // ERR_DIVISION_BY_ZERO,
    // ERR_TYPE_OP_LESS,
    // ERR_TYPE_OP_GREAT,
    // ERR_FELL_OFF_END,
    // ERR_UNKNOWN_OPCODE,
    ERR_UNDEFINED_CLASS_METHOD,
    ERR_WRONG_ARG_COUNT,
    // ERR_STACK_OVERFLOW,
    // ERR_INDEX_OUT_OF_BOUNDS,
    // ERR_PRINTLN_ARG_COUNT,
    ERR_CANNOT_ACCESS_FIELD_ON_CLASS,
    // ERR_NULL_REFERENCE,
    // ERR_INVALID_VTABLE_ACCESS,
    // ERR_UNIMPLEMENTED_VIRTUAL_METHOD,
    // ERR_NOT_CALLABLE,

    ERR_COUNT
} ErrorCode;

extern bool had_compile_error;
extern int  compile_error_count;

// システム通知・エラー API (多言語対応)
void report_info(ErrorCode code, ...);
void report_warning(ErrorCode code, ...);
void report_system_error(ErrorCode code, ...);

// パーサー構文エラー用
void error_syntax_at(Location loc, ErrorCode code, ...);

// void error_syntax(const char* filename, int line, int col_start, int col_end, const char* bison_msg);

// コンパイルエラー用
void error_at(Location loc, ErrorCode code, ...);
void error_compile(ErrorCode code, ...);

// // VM 実行時エラー用
// void error_runtime(ErrorCode code, ...);

// void report_undefined_message(const char* type_name, const char* message_name);

#endif
