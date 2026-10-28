#include "bcd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#ifndef SYSTEM_LIB_DIR
#define SYSTEM_LIB_DIR "/usr/local/lib/japeng"
#endif

// 正しい標準ライブラリ（.bcd / builtins.cd）の場所を特定して返す
const char* get_japeng_lib_dir(void) 
{
    // 1. 環境変数の確認
    const char* env_path = getenv("JAPENG_LIB");
    if (env_path != NULL) return env_path;

    // 2. システムインストール先 (/usr/local/lib/japeng) の確認
    struct stat st;
    if (stat(SYSTEM_LIB_DIR, &st) == 0 && S_ISDIR(st.st_mode)) {
        return SYSTEM_LIB_DIR;
    }

    // 3. ローカル開発環境フォールバック (./lib)
    if (stat("./lib", &st) == 0 && S_ISDIR(st.st_mode)) {
        return "./lib";
    }

    return SYSTEM_LIB_DIR; // デフォルト
}

// クラス名などの文字列を安全なキャッシュファイル名にエンコード (例: "猫" -> "%E7%8C%AB")
void encode_cache_name(const char* name, char* out, size_t max_len) 
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;

    for (size_t i = 0; name[i] != '\0' && o + 3 < max_len; i++) {
        unsigned char c = (unsigned char)name[i];
        // ASCII 英数字とハイフン・アンダースコアはそのまま保持
        if (isalnum(c) || c == '_' || c == '-') {
            out[o++] = c;
        } else {
            // 日本語バイト列や特殊記号を %XX に変換
            out[o++] = '%';
            out[o++] = hex[(c >> 4) & 0x0F];
            out[o++] = hex[c & 0x0F];
        }
    }
    out[o] = '\0';
}

// .je_cache/<エンコード名>.<ext> のフルパスを生成
void get_cache_filepath(const char* base_dir, const char* name, const char* ext, char* out, size_t max_len) 
{
    char safe_name[512];
    encode_cache_name(name, safe_name, sizeof(safe_name));
    snprintf(out, max_len, "%s/.je_cache/%s.%s", base_dir, safe_name, ext);
}
