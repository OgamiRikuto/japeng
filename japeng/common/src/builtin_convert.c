#include "builtin_convert.h"
#include <string.h>

// 予約語・構文キーワードテーブル
static const KeywordDef keyword_table[] = {
    // 既存の lex トークンに対応するもの
    { "class ",   "クラス" },
    //{ "class ",   "型定義" }, // 表記揺れもここに追加するだけで対応可能
    { "based ",   "基は"   },
    { "from ",    "委譲元は" },
    { "super ",    "親"     },   
    { "return ",    "返値は" },
    { "return ",  "返値"   },
    { "self ",    "自身"   },
    { "arg ",      "引数"   },
    { "ret ",      "返す型" },
    { "break ",    "脱出"   },
    { "continue ", "ループ再開" },
    { "if ",       "もし"},
    { "elif ",     "またもし"},
    { "else ",     "その他なら"},
    { "repeat ",   "繰り返す"},
    { "and",       "かつ"},
    { "or",        "または"},
    { "true",      "真"},
    { "false",     "偽"},
    { "null",      "空"},

    // プリプロセスで構文展開・正規化される独自予約語
    { " is ",     "は"     },

    { NULL, NULL } // 番兵
};

static const BuiltinDef builtin_table[] = {
    // --- 組み込み型 (BUILTIN_TYPE) ---
    { "Function",     "関数型",       BUILTIN_TYPE },
    { "Object",       "オブジェクト型", BUILTIN_TYPE },
    { "Integer",      "整数型",       BUILTIN_TYPE },
    { "SmallInteger", "小整数型",     BUILTIN_TYPE },
    { "BigInteger",   "大整数型",     BUILTIN_TYPE },
    { "Float",        "実数型",       BUILTIN_TYPE },
    { "SmallFloat",   "小実数型",     BUILTIN_TYPE },
    { "BigFloat",     "大実数型",     BUILTIN_TYPE },
    { "String",       "文字列型",     BUILTIN_TYPE },
    { "List",         "リスト型",     BUILTIN_TYPE },
    { "Boolean",      "論理型",       BUILTIN_TYPE },
    { "Void",         "無型",         BUILTIN_TYPE },
    
    // --- 組み込みメソッド (BUILTIN_METHOD) ---
    { "print",        "出力",         BUILTIN_METHOD },
    { "println",      "行出力",       BUILTIN_METHOD },
    { "input",        "入力",         BUILTIN_METHOD },
    { "pow",          "累乗",         BUILTIN_METHOD },
    { "gcd",          "最小公倍数",    BUILTIN_METHOD },
    { "init",         "初期化",       BUILTIN_METHOD },
    { "destroy",      "破棄",         BUILTIN_METHOD }, 
    { "get",          "取得",         BUILTIN_METHOD },
    { "length",       "長さ",         BUILTIN_METHOD },
    { "split",        "区切る",       BUILTIN_METHOD },
    { "trim",         "トリム",       BUILTIN_METHOD },
    { "push",         "追加",         BUILTIN_METHOD },
    { "set",          "変更",         BUILTIN_METHOD },
    { "swap",         "入れ替え",     BUILTIN_METHOD },
    { "map",          "マップ",       BUILTIN_METHOD },
    
    { NULL, NULL, 0 }
};

static const char* get_localized_name(const BuiltinDef* def, OutputLang lang) {
    switch (lang) {
        case LANG_JA: return def->ja;
        case LANG_EN: default: return def->canonical_en;
    }
}

const char* builtin_to_canonical(const char* name, size_t len, OutputLang lang, BuiltinKind* out_kind) {
    for (int i = 0; builtin_table[i].canonical_en != NULL; i++) {
        const char* loc_name = get_localized_name(&builtin_table[i], lang);
        if (loc_name && strlen(loc_name) == len && strncmp(name, loc_name, len) == 0) {
            if (out_kind) *out_kind = builtin_table[i].kind;
            return builtin_table[i].canonical_en;
        }
    }
    return NULL;
}

const BuiltinDef* builtin_match_prefix(const char* src, OutputLang lang, size_t* out_matched_len) {
    const BuiltinDef* best_match = NULL;
    size_t max_len = 0;

    for (int i = 0; builtin_table[i].canonical_en != NULL; i++) {
        const char* loc_name = get_localized_name(&builtin_table[i], lang);
        if (!loc_name) continue;

        size_t len = strlen(loc_name);
        if (strncmp(src, loc_name, len) == 0) {
            if (len > max_len) {
                max_len = len;
                best_match = &builtin_table[i];
            }
        }
    }

    if (best_match && out_matched_len) {
        *out_matched_len = max_len;
    }
    return best_match;
}

const char* builtin_from_canonical(const char* canonical_en, OutputLang lang) {
    if (!canonical_en) return "";
    if (lang == LANG_EN) return canonical_en;

    for (int i = 0; builtin_table[i].canonical_en != NULL; i++) {
        if (strcmp(canonical_en, builtin_table[i].canonical_en) == 0) {
            const char* loc_name = get_localized_name(&builtin_table[i], lang);
            return loc_name ? loc_name : canonical_en;
        }
    }
    return canonical_en;
}


static const char* get_keyword_localized(const KeywordDef* def, OutputLang lang) {
    switch (lang) {
        case LANG_JA: return def->ja;
        case LANG_EN: default: return def->replacement;
    }
}

// 予約語の最長一致検索
const KeywordDef* keyword_match_prefix(const char* src, OutputLang lang, size_t* out_matched_len) {
    const KeywordDef* best_match = NULL;
    size_t max_len = 0;

    for (int i = 0; keyword_table[i].replacement != NULL; i++) {
        const char* kw = get_keyword_localized(&keyword_table[i], lang);
        if (!kw) continue;

        size_t len = strlen(kw);
        if (strncmp(src, kw, len) == 0) {
            if (len > max_len) {
                max_len = len;
                best_match = &keyword_table[i];
            }
        }
    }

    if (best_match && out_matched_len) {
        *out_matched_len = max_len;
    }
    return best_match;
}
