#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

OutputLang current_lang = LANG_EN;

typedef struct {
    const char* en;
    const char* ja;
} ErrorMessage;

// エラーメッセージ辞書
static const ErrorMessage error_table[ERR_COUNT] = {
    // 構文エラー
    [ERR_SYNTAX_GENERIC] = {
        .en = "syntax error: %s",
        .ja = "構文エラー: %s"
    },
    [ERR_EXPECTED_SEMICOLON] = {
        .en = "expected ';' at end of statement.",
        .ja = "文の末尾に ';' が必要です。"
    },
    [ERR_UNEXPECTED_TOKEN] = {
        .en = "unexpected token '%s'.",
        .ja = "予期しないトークン '%s' です。"
    },

    // コンパイルエラー
    [ERR_UNDEFINED_VAR] = {
        .en = "Undefined variable '%s'.",
        .ja = "未定義の変数 '%s' が参照されました。"
    },
    [ERR_ASSIGN_UNDEFINED_VAR] = {
        .en = "Assignment to undefined variable '%s'.",
        .ja = "未定義の変数 '%s' への代入です。"
    },
    [ERR_UNDEFINED_VAR_OR_FIELD] = {
        .en = "Undefined variable or field '%s' for compound assignment.",
        .ja = "複合代入の対象 '%s' (変数またはフィールド) が未定義です。"
    },
    [ERR_COMPOUND_ASSIGN_TARGET] = {
        .en = "Left side of compound assignment must be an identifier.",
        .ja = "複合代入の左辺には識別子を指定してください。"
    },
    [ERR_TOO_MANY_LOCALS] = {
        .en = "Too many local variables in scope.",
        .ja = "スコープ内のローカル変数宣言数が上限を超えました。"
    },
    [ERR_BREAK_OUTSIDE_LOOP] = {
        .en = "'break' outside of loop.",
        .ja = "ループ外で 'break' を使用することはできません。"
    },
    [ERR_CONTINUE_OUTSIDE_LOOP] = {
        .en = "'continue' outside of loop.",
        .ja = "ループ外で 'continue' を使用することはできません。"
    },
    [ERR_DELEGATE_CLASS_NOT_FOUND] = {
        .en = "Delegate class '%s' not found for field '%s'.",
        .ja = "フィールド '%s' に指定された委譲元クラス '%s' が見つかりません。"
    },
    [ERR_DELEGATE_METHOD_NOT_FOUND] = {
        .en = "Method '%s' not found in delegate class '%s'.",
        .ja = "委譲元クラス '%s' にメソッド '%s' が存在しません。"
    },
    [ERR_METHOD_NO_IMPL] = {
        .en = "Method '%s' must have an implementation or a delegate source.",
        .ja = "メソッド '%s' には本体ブロックまたは委譲先クラスの指定が必要です。"
    },
    [ERR_CLASS_NOT_REGISTERED] = {
        .en = "Class '%s' not registered in skeleton pass.",
        .ja = "クラス '%s' の骨格情報が登録されていません。"
    },
    [ERR_CYCLIC_INHERITANCE] = {
        .en = "Cyclic inheritance detected involving class '%s'.",
        .ja = "クラス '%s' において循環継承が検出されました。"
    },
    [ERR_UNHANDLED_AST_KIND] = {
        .en = "Unhandled ASTNode kind: %d.",
        .ja = "未対応の AST ノード種別です: %d。"
    },

    // VM 実行時エラー
    [ERR_UNDEFINED_GLOBAL] = {
        .en = "Undefined global variable '%s'.",
        .ja = "未定義のグローバル変数 '%s' です。"
    },
    [ERR_CANNOT_INSTANTIATE] = {
        .en = "Cannot instantiate non-class value.",
        .ja = "クラス以外の値を new することはできません。"
    },
    [ERR_TYPE_OP_ADD] = {
        .en = "Type error in OP_ADD: Arguments must be numbers.",
        .ja = "加算 (OP_ADD) の型エラー: 数値同士である必要があります。"
    },
    [ERR_TYPE_OP_SUB] = {
        .en = "Type error in OP_SUB: Arguments must be numbers.",
        .ja = "減算 (OP_SUB) の型エラー: 数値同士である必要があります。"
    },
    [ERR_TYPE_OP_MUL] = {
        .en = "Type error in OP_MUL: Arguments must be numbers.",
        .ja = "乗算 (OP_MUL) の型エラー: 数値同士である必要があります。"
    },
    [ERR_TYPE_OP_DIV] = {
        .en = "Type error in OP_DIV: Arguments must be numbers.",
        .ja = "除算 (OP_DIV) の型エラー: 数値同士である必要があります。"
    },
    [ERR_DIVISION_BY_ZERO] = {
        .en = "Division by zero.",
        .ja = "ゼロ除算エラー: 0 で割ることはできません。"
    },
    [ERR_TYPE_OP_LESS] = {
        .en = "Type error in OP_LESS: Cannot compare non-numeric values.",
        .ja = "比較 (OP_LESS) の型エラー: 数値以外の値を比較することはできません。"
    },
    [ERR_TYPE_OP_GREAT] = {
        .en = "Type error in OP_GREAT: Cannot compare non-numeric values.",
        .ja = "比較 (OP_GREAT) の型エラー: 数値以外の値を比較することはできません。"
    },
    [ERR_FELL_OFF_END] = {
        .en = "Execution fell off the end of chunk without OP_RETURN.",
        .ja = "実行エラー: OP_RETURN なしでバイトコードの末尾に到達しました。"
    },
    [ERR_UNKNOWN_OPCODE] = {
        .en = "Unknown opcode: %d.",
        .ja = "未知のオペコードです: %d。"
    },
    [ERR_UNDEFINED_CLASS_METHOD] = {
        .en = "Undefined class method '%s' for '%s'.",
        .ja = "クラス '%s' にクラスメソッド '%s' が定義されていません。"
    },
    [ERR_WRONG_ARG_COUNT] = {
        .en = "Expected %d arguments but got %d.",
        .ja = "引数の個数が一致しません (期待値: %d, 実際: %d)。"
    },
    [ERR_STACK_OVERFLOW] = {
        .en = "Stack overflow: frame limit exceeded.",
        .ja = "スタックオーバーフロー: コールフレームの上限を超えました。"
    },
    [ERR_UNDEFINED_MESSAGE] = {
        .en = "Undefined message '%s'.",
        .ja = "未定義のメッセージ (メソッド) '%s' が送信されました。"
    },
    [ERR_INDEX_OUT_OF_BOUNDS] = {
        .en = "Index out of bounds (index: %d, size: %d).",
        .ja = "インデックスが範囲外です (インデックス: %d, サイズ: %d)。"
    },
    [ERR_PRINTLN_ARG_COUNT] = {
        .en = "'println' expects 0 or 1 argument.",
        .ja = "'println' に渡せる引数は 0 個または 1 個のみです。"
    }
};
// 空行またはコメント行かどうかを判定
static bool is_ignorable_line(const char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    if (*s == '\0') return true;
    if (s[0] == '/' && s[1] == '/') return true;
    return false;
}

// 直前の有効なコード行の末尾にキャレットを表示する
static bool print_previous_line_missing_semicolon(const char* filename, int current_err_line) {
    FILE* fp = fopen(filename, "r");
    if (!fp) return false;

    char lines[512][1024];
    int line_count = 0;
    while (line_count < 512 && fgets(lines[line_count], sizeof(lines[line_count]), fp)) {
        line_count++;
    }
    fclose(fp);

    // エラー行の手前から上に向かって、空行・コメント以外の有効行を探す
    for (int l = current_err_line - 1; l >= 1; l--) {
        if (l <= line_count && !is_ignorable_line(lines[l - 1])) {
            char* buf = lines[l - 1];
            buf[strcspn(buf, "\r\n")] = '\0';

            // 行末の非空白文字の直後を特定
            int end_col = (int)strlen(buf);
            while (end_col > 0 && (buf[end_col - 1] == ' ' || buf[end_col - 1] == '\t')) {
                end_col--;
            }
            int caret_col = end_col + 1;

            const char* label = (current_lang == LANG_JA) ? "構文エラー" : "syntax error";
            const char* msg   = (current_lang == LANG_JA) 
                ? "文の末尾に ';' が必要です。" 
                : "expected ';' at end of statement.";

            fprintf(stderr, "\033[1m%s:%d:%d: \033[1;31m%s:\033[0m\033[1m %s\033[0m\n",
                    filename, l, caret_col, label, msg);

            fprintf(stderr, " %4d | %s\n", l, buf);
            fprintf(stderr, "      | ");
            for (int i = 1; i < caret_col; i++) {
                fputc((buf[i - 1] == '\t') ? '\t' : ' ', stderr);
            }
            fprintf(stderr, "\033[1;32m^\033[0m\n\n");
            return true;
        }
    }
    return false;
}

static void print_snippet(const char* filename, int line, int col_start, int col_end) {
    if (!filename || filename[0] == '\0') return;
    FILE* f = fopen(filename, "r");
    if (!f) return;

    char buf[1024];
    int cur_line = 1;
    while (fgets(buf, sizeof(buf), f)) {
        if (cur_line == line) {
            buf[strcspn(buf, "\r\n")] = '\0';
            fprintf(stderr, " %4d | %s\n", line, buf);
            fprintf(stderr, "      | ");
            int len = (int)strlen(buf);
            for (int i = 1; i < col_start; i++) {
                char ch = (i - 1 < len) ? buf[i - 1] : ' ';
                fputc((ch == '\t') ? '\t' : ' ', stderr);
            }
            int width = (col_end >= col_start) ? (col_end - col_start + 1) : 1;
            fprintf(stderr, "\033[1;31m^");
            for (int i = 1; i < width; i++) fputc('~', stderr);
            fprintf(stderr, "\033[0m\n\n");
            break;
        }
        cur_line++;
    }
    fclose(f);
}

// 構文エラー
void error_syntax(const char* filename, int line, int col_start, int col_end, const char* bison_msg) {
    const char* fn = (filename && filename[0] != '\0') ? filename : "input";

    // ★ セミコロン抜けのパターンを判定
    // "expecting ;" または "expecting . or ;" の場合、直前行のセミコロン抜けの可能性が極めて高い
    bool is_semicolon_related = (strstr(bison_msg, "expecting ;") != NULL ||
                                 strstr(bison_msg, "expecting . or ;") != NULL);

    if (is_semicolon_related) {
        // 直前行の末尾にセミコロンがないか補正表示を試みる
        if (print_previous_line_missing_semicolon(fn, line)) {
            return; // 直前行の補正表示に成功したため終了
        }
    }

    // 通常の構文エラー出力
    const char* label = (current_lang == LANG_JA) ? "構文エラー" : "syntax error";
    fprintf(stderr, "\033[1m%s:%d:%d: \033[1;31m%s:\033[0m\033[1m %s\033[0m\n", 
            fn, line, col_start, label, bison_msg);

    print_snippet(fn, line, col_start, col_end);
}

// コンパイルエラー (位置指定)
void error_at(Location loc, ErrorCode code, ...) {
    const char* fn = (loc.filename && loc.filename[0] != '\0') ? loc.filename : "input";
    const char* label = (current_lang == LANG_JA) ? "エラー" : "error";
    const char* fmt = (current_lang == LANG_JA) ? error_table[code].ja : error_table[code].en;

    fprintf(stderr, "\033[1m%s:%d:%d: \033[1;31m%s:\033[0m\033[1m ", fn, loc.line, loc.column, label);

    va_list args;
    va_start(args, code);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\033[0m\n");
    
    if (loc.line > 0) {
        print_snippet(fn, loc.line, loc.column, loc.column);
    }
}

// VM 実行時エラー
void error_runtime(ErrorCode code, ...) {
    const char* label = (current_lang == LANG_JA) ? "実行時エラー" : "Runtime Error";
    const char* fmt = (current_lang == LANG_JA) ? error_table[code].ja : error_table[code].en;

    fprintf(stderr, "\033[1;31m%s:\033[0m ", label);

    va_list args;
    va_start(args, code);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
}
