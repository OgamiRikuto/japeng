#include "token.h"
#include "language.h"
#include "builtin_convert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char* key;
    const char* en;
    char* other;
} TokenDictEntry;

static TokenDictEntry token_dict[TOKEN_COUNT] = {
    [TOK_EOF]           = {"TOK_EOF",           "end of file",               NULL},
    [CLASS]             = {"CLASS",             "class",                     NULL},
    [INTERFACE]         = {"INTERFACE",         "interface",                 NULL},
    [BASED]             = {"BASED",             "based",                     NULL},
    [FROM]              = {"FROM",              "from",                      NULL},
    [IMPLEMENTS]        = {"IMPLEMENTS",        "implements",                NULL},
    [ARG]               = {"ARG",               "arg",                       NULL},
    [RET]               = {"RET",               "ret",                       NULL},
    [RETURN]            = {"RETURN",            "return",                    NULL},
    [BREAK]             = {"BREAK",             "break",                     NULL},
    [CONTINUE]          = {"CONTINUE",          "continue",                  NULL},
    [STATIC]            = {"STATIC",            "static",                    NULL},
    [IMPORT]            = {"IMPORT",            "import",                    NULL},
    [AS]                = {"AS",                "as",                        NULL},
    [ELLIPSIS]          = {"ELLIPSIS",          "...",                       NULL},
    [COMMA]             = {"COMMA",             "','",                       NULL},
    [DOT]               = {"DOT",               "'.'",                       NULL},
    [COLON]             = {"COLON",             "':'",                       NULL},
    [SEMICOLON]         = {"SEMICOLON",         "';'",                       NULL},
    [L_PAR]             = {"L_PAR",             "(",                         NULL},
    [R_PAR]             = {"R_PAR",             ")",                         NULL},
    [L_BLACKET]         = {"L_BLACKET",         "[",                         NULL},
    [R_BLACKET]         = {"R_BLACKET",         "]",                         NULL},
    [GT]                = {"GT",                ">",                         NULL},
    [LT]                = {"LT",                "<",                         NULL},
    [SHR]               = {"SHR",               ">>",                        NULL},
    [UNKNOWN]           = {"UNKNOWN",           "unknown",                   NULL},
    [SELF]              = {"SELF",              "self",                      NULL},
    [IDENTIFIER]        = {"IDENTIFIER",        "identifier",                NULL},
    [CLASS_NAME]        = {"CLASS_NAME",        "class name",                NULL},
    [SP_IDENTIFIER]     = {"SP_IDENTIFIER",     "operator",                  NULL},
    [ASSIGN_IDENTIFIER] = {"ASSIGN_IDENTIFIER", "=",                         NULL},
    [STRING]            = {"STRING",            "string",                    NULL},
    [FRACTION]          = {"FRACTION",          "fraction",                  NULL},
    [INTEGER]           = {"INTEGER",           "integer",                   NULL},
    [FLOAT]             = {"FLOAT",             "float",                     NULL},
    [BINARY]            = {"BINARY",            "binary",                    NULL},
    [HEX]               = {"HEX",               "hex",                       NULL},
    [TRUE_LITERAL]      = {"TRUE_LITERAL",      "true",                      NULL},
    [FALSE_LITERAL]     = {"FALSE_LITERAL",     "false",                     NULL},
    [NULL_LITERAL]      = {"NULL_LITERAL",      "null",                      NULL},
    [STR_OPEN]          = {"STR_OPEN",          "string open",               NULL},
    [STR_CLOSE]         = {"STR_CLOSE",         "string close",              NULL},
    [STR_EXPR_OPEN]     = {"STR_EXPR_OPEN",     "string expression open",    NULL},
    [STR_EXPR_CLOSE]    = {"STR_EXPR_CLOSE",    "string expression close",   NULL},
    [STR_TEXT]          = {"STR_TEXT",          "string text",               NULL},
};

static char loaded_token_lang[32] = {0};

static void register_token_name_cb(const char* key, const char* val, void* user_data) {
    (void)user_data;
    for (int i = 0; i < TOKEN_COUNT; i++) {
        if (token_dict[i].key && strcmp(token_dict[i].key, key) == 0) {
            token_dict[i].other = strdup(val);
            break;
        }
    }
}

static void ensure_token_dict_loaded(const char* lang_code) {
    if (!lang_code || strcmp(lang_code, "en") == 0) return;
    if (strcmp(loaded_token_lang, lang_code) == 0) return;

    for (int i = 0; i < TOKEN_COUNT; i++) {
        free(token_dict[i].other);
        token_dict[i].other = NULL;
    }
    strncpy(loaded_token_lang, lang_code, sizeof(loaded_token_lang) - 1);

    char lib_dir[1024];
    get_japeng_lib_dir_buf(lib_dir, sizeof(lib_dir));

    char dict_path[1024];
    snprintf(dict_path, sizeof(dict_path), "%s/lang/tokens.dict.%s", lib_dir, lang_code);
    parse_dict_file(dict_path, register_token_name_cb, NULL);
}

const char* parser_get_token_name(TokenType type, OutputLang lang) {
    if (type >= 0 && type < TOKEN_COUNT) {
        const char* lang_code = output_lang_to_code(lang);
        if (strcmp(lang_code, "en") != 0) {
            ensure_token_dict_loaded(lang_code);
            if (token_dict[type].other && token_dict[type].other[0] != '\0') {
                return token_dict[type].other;
            }
        }
        if (token_dict[type].en) return token_dict[type].en;
    }
    return "token";
}
