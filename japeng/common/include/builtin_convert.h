#ifndef BUILTIN_CONVERT_H
#define BUILTIN_CONVERT_H

#include <stdbool.h>
#include <stddef.h>
#include "language.h"

typedef struct {
    char* canonical_en;
    char* localized;
} DictEntry;

typedef void (*DictEntryCallback)(const char* key, const char* value, void* user_data);

// 共通ヘルパー（他ファイルからも利用）
bool parse_dict_file(const char* filepath, DictEntryCallback callback, void* user_data);
void get_japeng_lib_dir_buf(char* out_buf, size_t buf_size);

// OutputLang から言語コード文字列 ("en", "ja" 等) を取得
const char* output_lang_to_code(OutputLang lang);

// 1. ローカライズ名から内部英語名を取得（完全一致
const char* builtin_from_canonical(const char* canonical_en, OutputLang lang);

// 2. テキスト先頭から最長一致する組み込み単語を検索（プリプロセッサ用）
const DictEntry* builtin_match_prefix(const char* src, const char* lang_code, size_t* out_matched_len);

// 3. 内部英語名から表示用ローカライズ名を取得（エラー表示用）
const char* builtin_from_canonical(const char* canonical_en, OutputLang lang);

// 4. 予約語・助詞ルールの最長一致検索（プリプロセッサ用）
const DictEntry* keyword_match_prefix(const char* src, const char* lang_code, size_t* out_matched_len);

#endif
