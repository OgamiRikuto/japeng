#ifndef BUILTIN_CONVERT_H
#define BUILTIN_CONVERT_H

#include <stdbool.h>
#include <stddef.h>
#include "error.h"

typedef enum {
    BUILTIN_TYPE,    // クラス・型名（Flexで CLASS_NAME 扱い）
    BUILTIN_METHOD,  // メソッド名（Flexで IDENTIFIER 扱い）
    BUILTIN_FIELD,   // フィールド名
    BUILTIN_MAX
} BuiltinKind;

typedef struct {
    const char* canonical_en; // 内部の正準名（String, println など）
    const char* ja;           // 日本語名（文字列型, 行出力 など）
    BuiltinKind kind;
} BuiltinDef;

// --- 予約語・構文キーワード ---
typedef struct {
    const char* replacement; // 置換後の文字列（例: " is ", "class ", "return "）
    const char* ja;          // 日本語キーワード（例: "は", "クラス", "返す"）
} KeywordDef;

// 1. 各言語の名称から「内部英語名」を取得（完全一致）
const char* builtin_to_canonical(const char* name, size_t len, OutputLang lang, BuiltinKind* out_kind);

// 2. テキスト先頭から最長一致する組み込み単語を検索（プリプロセッサ用）
const BuiltinDef* builtin_match_prefix(const char* src, OutputLang lang, size_t* out_matched_len);

// 3. 内部英語名から「表示用ローカライズ名」を取得（エラー表示・ログ用）
const char* builtin_from_canonical(const char* canonical_en, OutputLang lang);

// 予約語検索（プリプロセッサ用）
const KeywordDef* keyword_match_prefix(const char* src, OutputLang lang, size_t* out_matched_len);
#endif
