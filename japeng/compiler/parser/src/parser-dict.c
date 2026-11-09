// compiler/parser/src/parser_dict.c
#include "token.h"
#include "language.h"

typedef struct {
    const char* en;
    const char* ja;
} TokenNameEntry;

// パーサー国家の専売特許テーブル
static const TokenNameEntry token_dict[TOKEN_COUNT] = {
    // 終端
    [TOK_EOF] = {"end of file", "ファイルの終端"},

    // 予約語
    [CLASS] = {"class", "クラス"},
    [INTERFACE] = {"interface", "インターフェース"},
    [BASED] = {"based", "基は"},
    [FROM] = {"from", "委譲元は"},
    [IMPLEMENTS] = {"implements", "実装元は"},
    [ARG] = {"arg", "引数"},
    [RET] = {"ret", "返り値の型"},
    [RETURN] = {"return", "返値は"},
    [BREAK] = {"break", "脱出"},
    [CONTINUE] = {"continue", "ループ再開"},
    [STATIC] = {"static", "静的な"},
    [IMPORT] = {"import", "導入"},
    [AS] = {"as", "アズ"},
    [ELLIPSIS] = {"...", "・・・"},

    // 記号
    [COMMA] = {"','", "'、'"},
    [DOT] = {"'.'", "'・'"},
    [COLON] = {"':'", "':'"},
    [SEMICOLON] = {"';'", "'。'"},
    [L_PAR] = {"(", "("},
    [R_PAR] = {")", ")"},
    [L_BLACKET] = {"[", "『"},
    [R_BLACKET] = {"]", "』"},
    [UNKNOWN] = {"unknown", "未知"},

    // 識別子・リテラル
    [SELF] = {"self", "自身"},
    [IDENTIFIER] = {"identifier", "識別子"},
    [CLASS_NAME] = {"class name", "クラス名"},
    [SP_IDENTIFIER] = {"operator", "演算子"},
    [ASSIGN_IDENTIFIER] = {"is or are", "は"},
    [STRING] = {"string", "文字列"},
    [FRACTION] = {"fraction", "分数"},
    [INTEGER] = {"integer", "整数"},
    [FLOAT] = {"float", "小数"},
    [BINARY] = {"binary", "二進数"},
    [HEX] = {"hex", "十進数"},
    [TRUE_LITERAL] = {"true", "真"},
    [FALSE_LITERAL] = {"false", "偽"},
    [NULL_LITERAL] = {"null", "無"},

    // 文字列補間
    [STR_OPEN] = {"string open", "文字列の始まり"},
    [STR_CLOSE] = {"string close", "文字列の終わり"},
    [STR_EXPR_OPEN] = {"string expression open", "文字列変数の始まり"},
    [STR_EXPR_CLOSE] = {"string expression close", "文字列変数の終わり"},
    [STR_TEXT] = {"string text", "文字列本体"},
};

const char* parser_get_token_name(TokenType type, OutputLang lang) 
{
    if (type >= 0 && type < TOKEN_COUNT) {
        const char* s = (lang == LANG_JA) ? token_dict[type].ja : token_dict[type].en;
        if (s && s[0] != '\0') return s;
        return token_dict[type].en;
    }
    return "token";
}
