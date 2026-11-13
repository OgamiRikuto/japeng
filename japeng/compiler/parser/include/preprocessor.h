#ifndef PREPROCESSOR_H_
#define PREPROCESSOR_H_

#include <stdbool.h>
#include "compiler-error.h"

// 各自然言語フロントエンドが満たすべき共通インターフェース
typedef struct {
    OutputLang  lang;              // LANG_JA, LANG_DE など
    const char* ext_script;
    const char* ext_class;

    char*       (*preprocess)(const char* src);
    void        (*map_loc)(int clean_line, int clean_col, int* out_line, int* out_col);
    void        (*reset)(void);
    const char* (*lookup_symbol)(const char* placeholder);

    // 外部モジュール辞書の動的登録
    void        (*register_module_dict)(const char* canonical_name, const char* localized_name);
} LanguageFrontend;

// --- ハブ API ---
LanguageFrontend* find_frontend_by_ext(const char* ext);
void set_active_frontend(LanguageFrontend* frontend);
LanguageFrontend* get_active_frontend(void);

char*       frontend_preprocess(const char* src);
void        frontend_map_loc(int clean_line, int clean_col, int* out_line, int* out_col);
void        frontend_reset(void);
const char* frontend_lookup_symbol(const char* placeholder);

#endif // PREPROCESSOR_H_
