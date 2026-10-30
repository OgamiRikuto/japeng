#include "error.h"
#include "builtin_convert.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

OutputLang current_lang = LANG_EN;
OutputLang compile_lang = LANG_EN;

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
    [ERR_SYNTAX_UNEXPECTED] = {
        .en = "syntax error: unexpected %s.",
        .ja = "構文エラー: 予期しない %s です。"
    },
    [ERR_SYNTAX_UNEXPECTED_EXPECTING] = {
        .en = "syntax error: unexpected %1$s, expecting %2$s.",
        .ja = "構文エラー: 予期しない %1$s です (%2$s が必要です)。"
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
        .en = "Delegate class '%1$s' not found for field '%2$s'.",
        .ja = "フィールド '%2$s' に指定された委譲元クラス '%1$s' が見つかりません。"
    },
    [ERR_DELEGATE_METHOD_NOT_FOUND] = {
        .en = "Method '%1$s' not found in delegate class '%2$s'.",
        .ja = "委譲元クラス '%2$s' にメソッド '%1$s' が存在しません。"
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
    [ERR_FIELD_INIT_MUST_BE_LITERAL] = {
        .en = "Field '%s' initial value must be a literal.",
        .ja = "フィールド '%s' の初期値にはリテラルを指定してください。"
    },
    [ERR_UNHANDLED_AST_KIND] = {
        .en = "Unhandled ASTNode kind: %d.",
        .ja = "未対応の AST ノード種別です: %d。"
    },
    [ERR_FLOAT_OVERFLOW] = {
        .en = "Float literal '%s' exceeds representable range.",
        .ja = "浮動小数点数リテラル '%s' が表現範囲を超えています。"
    },
    [ERR_TYPE_MISMATCH] = {
        .en = "Type mismatch: expected '%1$s', got '%2$s'.",
        .ja = "型の不一致です (期待される型: '%1$s', 実際の型: '%2$s')。"
    },
    [ERR_ASSIGN_TYPE_MISMATCH] = {
    .en = "Cannot assign value of type '%1$s' to variable '%2$s' of type '%3$s'.",
    .ja = "型 '%3$s' の変数 '%2$s' に型 '%1$s' の値を代入することはできません。"
},
    [ERR_ARG_TYPE_MISMATCH] = {
        .en = "Type mismatch in argument %1$d: expected '%2$s', got '%3$s'.",
        .ja = "第 %1$d 引数の型が一致しません (期待される型: '%2$s', 実際の型: '%3$s')。"
    },
    [ERR_ARITY_MISMATCH] = {
        .en = "Arity mismatch: expected %1$d arguments, got %2$d.",
        .ja = "引数の個数が一致しません (期待値: %1$d, 実際: %2$d)。"
    },
    [ERR_RETURN_TYPE_MISMATCH] = {
        .en = "Return type mismatch: expected '%1$s', got '%2$s'.",
        .ja = "戻り値の型が一致しません (期待される型: '%1$s', 実際の型: '%2$s')。"
    },
    [ERR_NON_NUMERIC_OPERATION] = {
        .en = "Operator '%1$s' requires numeric operands, but got '%2$s'.",
        .ja = "演算 '%1$s' には数値型が必要ですが、'%2$s' が渡されました。"
    },
    [ERR_DEFAULT_ARG_ORDER] = {
        .en = "Parameter without default value cannot follow parameter with default value.",
        .ja = "デフォルト値を持つ引数の後にデフォルト値のない引数を配置することはできません。"
    },
    [ERR_ARITY_RANGE_MISMATCH] = {
        .en = "Arity mismatch: expected %1$d to %2$d arguments, got %3$d.",
        .ja = "引数の個数が一致しません (期待値: %1$d〜%2$d, 実際: %3$d)。"
    },
    [ERR_ARG_INIT_MUST_BE_LITERAL] = {
        .en = "Default value for parameter '%s' must be a literal.",
        .ja = "引数 '%s' のデフォルト値にはリテラルを指定してください。"
    },
    [ERR_FUNCTION_REQUIRES_SIGNATURE] = {
        .en = "Function declaration for '%s' requires a signature (e.g. 'Function(;)' or 'Function(Arg; Ret)').",
        .ja = "'%s' の関数型宣言にはシグネチャの指定が必要です (例: 'Function(;)' や 'Function(引数型; 戻り値型)')。"
    },
    [ERR_BUILTIN_CACHE] = {
        .en = "Cache of builtin classes is not newest. Please remove the cache once, and re-compile.",
        .ja = "組み込み関数のキャッシュが最新ではありません。一度キャッシュを削除してコンパイルし直してください。"
    },
    [ERR_IF_COND_NOT_BOOL] = {
        .en = "Condition of '%1$s' must be of Boolean type, but got '%2$s'.",
        .ja = "'%1$s' の条件式には真偽値型 (Boolean) が必要ですが、'%2$s' が渡されました。"
    },
    [ERR_REPEAT_COND_NOT_BOOL] = {
        .en = "Condition of 'repeat' loop must be of Boolean type, but got '%s'.",
        .ja = "条件ループ 'repeat' (繰り返す) の条件式には真偽値型 (Boolean) が必要ですが、'%s' が渡されました。"
    },
    [ERR_REPEAT_LIMIT_NOT_INT] = {
        .en = "Count limit for 'repeat' loop must be an Integer, but got '%s'.",
        .ja = "回数指定ループ 'repeat' (繰り返す) の回数には整数型 (Integer) が必要ですが、'%s' が渡されました。"
    },
    [ERR_EACH_NOT_LIST] = {
        .en = "'each' can only be applied to a List, but got '%s'.",
        .ja = "'each' (各々) はリスト型 (List) にのみ適用できますが、'%s' が渡されました。"
    },
    [ERR_EACH_ARITY] = {
        .en = "Block for 'each' must take 1 (element) or 2 (index, element) arguments, but got %d.",
        .ja = "'each' (各々) のブロックには引数が 1 個 (要素) または 2 個 (添字, 要素) 必要ですが、%d 個指定されました。"
    },
    [ERR_EACH_INDEX_TYPE] = {
        .en = "First argument (index) of 'each' must be of type 'Integer', but got '%s'.",
        .ja = "'each' (各々) の第 1 引数 (添字) には 'Integer' (整数型) が必要ですが、'%s' が指定されました。"
    },
    [ERR_EACH_ELEMENT_TYPE] = {
        .en = "Element argument of 'each' expects type '%1$s', but got '%2$s'.",
        .ja = "'each' (各々) の要素引数には型 '%1$s' が必要ですが、'%2$s' が指定されました。"
    },
    [ERR_INDEX_KEY_NOT_INT] = {
        .en = "Index inside '[]' must be an Integer, but got '%s'.",
        .ja = "添字 '[]' の中身には整数型 (Integer) を指定する必要がありますが、'%s' が渡されました。"
    },
    [ERR_INDEX_TARGET_INVALID] = {
        .en = "Cannot index into value of type '%s' (only List and String are supported).",
        .ja = "型 '%s' に対するインデックスアクセス '[]' は無効です (List または String のみ対応)。"
    },
    [ERR_BITWISE_OP_TYPE] = {
        .en = "Bitwise operator '%1$s' requires Integer operands, but got '%2$s'.",
        .ja = "ビット演算子 '%1$s' には整数型 (Integer) が必要ですが、'%2$s' が渡されました。"
    },
    [ERR_LOGICAL_OP_TYPE] = {
        .en = "Logical operator '%1$s' requires Boolean operands, but got '%2$s'.",
        .ja = "論理演算子 '%1$s' には真偽値型 (Boolean) が必要ですが、'%2$s' が渡されました。"
    },
    [ERR_MULTI_ASSIGN_COUNT_MISMATCH] = {
        .en = "Multi-assignment mismatch: %1$d variable(s) on left, but %2$d value(s) on right.",
        .ja = "複数代入の要素数が一致しません (左辺: %1$d 個の変数, 右辺: %2$d 個の値)。"
    },
    [ERR_BINARY_OP_TYPE_MISMATCH] = {
        .en = "Operator '%1$s' cannot be applied to different types: '%2$s' and '%3$s'.",
        .ja = "演算子 '%1$s' の両辺の型が一致しません (左辺: '%2$s', 右辺: '%3$s')。"
    },
    [ERR_METHOD_ARITY_MISMATCH] = {
        .en = "Method '%1$s' expects %2$d argument(s), but got %3$d.",
        .ja = "メソッド '%1$s' に渡された引数の個数が一致しません (期待値: %2$d 個, 実際: %3$d 個)。"
    },
    [ERR_METHOD_ARITY_RANGE] = {
        .en = "Method '%1$s' expects %2$d to %3$d arguments, but got %4$d.",
        .ja = "メソッド '%1$s' に渡された引数の個数が一致しません (期待値: %2$d〜%3$d 個, 実際: %4$d 個)。"
    },
    [ERR_METHOD_ARG_TYPE_MISMATCH] = {
        .en = "Type mismatch in argument %2$d of method '%1$s': expected '%3$s', but got '%4$s'.",
        .ja = "メソッド '%1$s' の第 %2$d 引数の型が一致しません (期待される型: '%3$s', 実際の型: '%4$s')。"
    },
    [ERR_FUNC_ARITY_MISMATCH] = {
        .en = "Function '%1$s' expects %2$d argument(s), but got %3$d.",
        .ja = "関数 '%1$s' に渡された引数の個数が一致しません (期待値: %2$d 個, 実際: %3$d 個)。"
    },
    [ERR_FUNC_ARG_TYPE_MISMATCH] = {
        .en = "Type mismatch in argument %2$d of function '%1$s': expected '%3$s', but got '%4$s'.",
        .ja = "関数 '%1$s' の第 %2$d 引数の型が一致しません (期待される型: '%3$s', 実際の型: '%4$s')。"
    },
    [ERR_RETURN_EMPTY_IN_VALUE_FUNC] = {
        .en = "Function declared to return '%s' must return a value, but 'return' is empty.",
        .ja = "戻り値型 '%s' が宣言されている関数では値を返す必要がありますが、値が指定されていません。"
    },
    [ERR_RETURN_VALUE_IN_VOID_FUNC] = {
        .en = "Cannot return a value from a function with 'Void' return type (attempted to return '%s').",
        .ja = "戻り値型が 'Void' (無型) の関数内では値を返せません ('%s' が返されました)。"
    },
    [ERR_CASE_TYPE_MISMATCH] = {
        .en = "Condition in 'case' must match target type '%1$s', but got '%2$s'.",
        .ja = "'case' (場合) の条件式には対象と同じ型 '%1$s' が必要ですが、'%2$s' が渡されました。"
    },
    [ERR_INDEX_ASSIGN_TYPE_MISMATCH] = {
        .en = "Cannot assign value of type '%1$s' to element of type '%2$s'.",
        .ja = "型 '%2$s' の要素に型 '%1$s' の値を代入することはできません。"
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
        .en = "Undefined class method '%1$s' for '%2$s'.",
        .ja = "クラス '%2$s' にクラスメソッド '%1$s' が定義されていません。"
    },
    [ERR_WRONG_ARG_COUNT] = {
        .en = "Expected %1$d arguments but got %2$d.",
        .ja = "引数の個数が一致しません (期待値: %1$d, 実際: %2$d)。"
    },
    [ERR_STACK_OVERFLOW] = {
        .en = "Stack overflow: frame limit exceeded.",
        .ja = "スタックオーバーフロー: コールフレームの上限を超えました。"
    },
    [ERR_UNDEFINED_MESSAGE] = {
        .en = "Undefined message '%1$s' for '%2$s'.",
        .ja = "型 '%2$s' に未定義のメッセージ (メソッド) '%1$s' が送信されました。"
    },
    [ERR_INDEX_OUT_OF_BOUNDS] = {
        .en = "Index out of bounds (index: %1$d, size: %2$d).",
        .ja = "インデックスが範囲外です (インデックス: %1$d, サイズ: %2$d)。"
    },
    [ERR_PRINTLN_ARG_COUNT] = {
        .en = "'println' expects 0 or 1 argument.",
        .ja = "'println' に渡せる引数は 0 個または 1 個のみです。"
    },
    [ERR_CANNOT_ACCESS_FIELD_ON_CLASS] = {
        .en = "Cannot access instance field from an uninstantiated class.",
        .ja = "インスタンス化されていないクラスからフィールドにアクセスすることはできません。"
    },
    [ERR_NULL_REFERENCE] = {
        .en = "Null reference error: Attempted to access '%s' on null.",
        .ja = "Null参照エラー: null に対する '%s' のアクセスまたは呼び出しです。"
    },
    [ERR_INVALID_VTABLE_ACCESS] = {
        .en = "Invalid virtual method table access (index: %d).",
        .ja = "仮想関数テーブルへの不正なアクセスです (インデックス: %d)。"
    },
    [ERR_UNIMPLEMENTED_VIRTUAL_METHOD] = {
        .en = "Attempted to call an unimplemented virtual method (index: %d).",
        .ja = "未実装の仮想メソッドが呼び出されました (インデックス: %d)。"
    },
    [ERR_NOT_CALLABLE] = {
        .en = "Target object is not callable.",
        .ja = "呼び出し可能なオブジェクトではありません。"
    }
};

typedef struct {
    const char* raw;
    const char* en;
    const char* ja;
} TokenNameMap;

static const TokenNameMap token_names[] = {
    { "identifier",   "identifier",       "識別子" },
    { "operator",     "operator",         "演算子" },
    { "class name",   "class name",       "クラス名" },
    { "integer",      "integer literal",  "整数リテラル" },
    { "float",        "float literal",    "小数リテラル" },
    { "fraction",     "fraction literal", "分数リテラル" },
    { "string",       "string literal",   "文字列リテラル" },
    { "self",         "'self'",           "'自身' (self)" },
    { "class",        "'class'",          "'クラス' (class)" },
    { "based",        "'based'",          "'基底' (based)" },
    { "from",         "'from'",           "'委譲' (from)" },
    { "arg",          "'arg'",            "'引数' (arg)" },
    { "ret",          "'ret'",            "'返す型' (ret)" },
    { "return",       "'return'",         "'返値は' (return)" },
    { "break",        "'break'",          "'中断' (break)" },
    { "continue",     "'continue'",       "'再開' (continue)" },
    { "$end",         "end of file",      "ファイルの終端" },
    { "end of file",  "end of file",      "ファイルの終端" },
    { "$undefined",   "invalid token",    "無効なトークン" },
    { ",",            "','",              "',' (読点)" },
    { ".",            "'.'",              "'.' (中黒)" },
    { ":",            "':'",              "':' (コロン)" },
    { ";",            "';'",              "';' (句点)" },
    { "(",            "'('",              "'(' (丸括弧)" },
    { ")",            "')'",              "')' (丸括弧閉)" },
    { "[",            "'['",              "'[' (括弧)" },
    { "]",            "']'",              "']' (括弧閉)" },
    { NULL, NULL, NULL }
};

// 空行またはコメント行かどうかを判定
static bool is_ignorable_line(const char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    if (*s == '\0') return true;
    if (s[0] == '/' && s[1] == '/') return true;
    return false;
}

static const char* translate_token(const char* raw, OutputLang lang) {
    if (!raw) return "";
    for (int i = 0; token_names[i].raw != NULL; i++) {
        if (strcmp(raw, token_names[i].raw) == 0) {
            return (lang == LANG_JA) ? token_names[i].ja : token_names[i].en;
        }
    }
    return raw;
}

// 期待されるトークン群（"expecting A or B or C"）を言語別に整形
static void translate_expecting_list(const char* raw_expecting, char* out_buf, size_t out_size, OutputLang lang) {
    char temp[512];
    strncpy(temp, raw_expecting, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    out_buf[0] = '\0';
    char* token = strtok(temp, " ");
    bool first = true;

    while (token != NULL) {
        if (strcmp(token, "or") == 0) {
            token = strtok(NULL, " ");
            continue;
        }
        const char* trans = translate_token(token, lang);
        if (!first) {
            strncat(out_buf, (lang == LANG_JA) ? " または " : " or ", out_size - strlen(out_buf) - 1);
        }
        strncat(out_buf, trans, out_size - strlen(out_buf) - 1);
        first = false;
        token = strtok(NULL, " ");
    }
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

    // セミコロン抜けの先行補正
    bool is_semicolon_related = (strstr(bison_msg, "expecting ;") != NULL ||
                                 strstr(bison_msg, "expecting . or ;") != NULL);
    if (is_semicolon_related) {
        if (print_previous_line_missing_semicolon(fn, line)) {
            return;
        }
    }

    const char* label = (current_lang == LANG_JA) ? "構文エラー" : "syntax error";
    char formatted_msg[1024];

    // Bison メッセージの解析: "syntax error, unexpected TOKEN, expecting EXPECTED..."
    const char* unexpected_prefix = "unexpected ";
    const char* expecting_prefix  = ", expecting ";
    const char* unexp_pos = strstr(bison_msg, unexpected_prefix);

    if (unexp_pos != NULL) {
        unexp_pos += strlen(unexpected_prefix);
        const char* exp_pos = strstr(unexp_pos, expecting_prefix);

        char unexp_tok[128];
        if (exp_pos != NULL) {
            size_t len = exp_pos - unexp_pos;
            if (len >= sizeof(unexp_tok)) len = sizeof(unexp_tok) - 1;
            strncpy(unexp_tok, unexp_pos, len);
            unexp_tok[len] = '\0';

            const char* exp_tokens_raw = exp_pos + strlen(expecting_prefix);
            char translated_exp[512];
            translate_expecting_list(exp_tokens_raw, translated_exp, sizeof(translated_exp), current_lang);

            const char* translated_unexp = translate_token(unexp_tok, current_lang);

            if (current_lang == LANG_JA) {
                snprintf(formatted_msg, sizeof(formatted_msg), "予期しない %s です (%s が必要です)。",
                         translated_unexp, translated_exp);
            } else {
                snprintf(formatted_msg, sizeof(formatted_msg), "unexpected %s, expecting %s.",
                         translated_unexp, translated_exp);
            }
        } else {
            strncpy(unexp_tok, unexp_pos, sizeof(unexp_tok) - 1);
            unexp_tok[sizeof(unexp_tok) - 1] = '\0';
            const char* translated_unexp = translate_token(unexp_tok, current_lang);

            if (current_lang == LANG_JA) {
                snprintf(formatted_msg, sizeof(formatted_msg), "予期しない %s です。", translated_unexp);
            } else {
                snprintf(formatted_msg, sizeof(formatted_msg), "unexpected %s.", translated_unexp);
            }
        }
    } else {
        // パースできなかった汎用フォールバック
        snprintf(formatted_msg, sizeof(formatted_msg), "%s", bison_msg);
    }

    fprintf(stderr, "\033[1m%s:%d:%d: \033[1;31m%s:\033[0m\033[1m %s\033[0m\n", 
            fn, line, col_start, label, formatted_msg);

    print_snippet(fn, line, col_start, col_end);
}

bool had_compile_error = false;
int  compile_error_count = 0;

// コンパイルエラー (位置指定)
void error_at(Location loc, ErrorCode code, ...) {
    had_compile_error = true;
    compile_error_count++;

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

// 未定義メソッド送信エラーの例 (error.c)
void report_undefined_message(const char* type_name, const char* message_name) 
{
    error_runtime(ERR_UNDEFINED_MESSAGE, 
        builtin_from_canonical(message_name, current_lang),
        builtin_from_canonical(type_name, current_lang));
}
