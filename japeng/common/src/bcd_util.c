#include "bcd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <limits.h>

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

// パスを正規化（存在すれば realpath、無ければそのままコピー）
static void normalize_abs_path(const char* path, char* out, size_t max_len)
{
    if (!path || path[0] == '\0') {
        out[0] = '\0';
        return;
    }
    char resolved[PATH_MAX];
    if (realpath(path, resolved) != NULL) {
        strncpy(out, resolved, max_len - 1);
        out[max_len - 1] = '\0';
    } else {
        strncpy(out, path, max_len - 1);
        out[max_len - 1] = '\0';
    }
}

// abs_path が abs_root 配下にあれば相対パス部分を out_rel に取り出す
static bool strip_root_prefix(const char* abs_path, const char* abs_root, char* out_rel, size_t max_len)
{
    if (abs_root[0] == '\0') return false;
    size_t root_len = strlen(abs_root);
    if (strncmp(abs_path, abs_root, root_len) == 0 && abs_path[root_len] == '/') {
        strncpy(out_rel, abs_path + root_len + 1, max_len - 1);
        out_rel[max_len - 1] = '\0';
        return true;
    }
    return false;
}

void make_canonical_id(const char* project_dir, const char* file_path,
                       const char* class_name, bool is_builtin_class,
                       char* out, size_t max_len)
{
    if (is_builtin_class || !file_path || file_path[0] == '\0') {
        snprintf(out, max_len, "builtin::%s", class_name);
        return;
    }

    char abs_file[PATH_MAX];
    normalize_abs_path(file_path, abs_file, sizeof(abs_file));

    const char* prefix = "proj";
    char rel_path[PATH_MAX] = {0};

    // 1. 標準ライブラリ (lib/) 判定
    char abs_lib[PATH_MAX];
    normalize_abs_path(get_japeng_lib_dir(), abs_lib, sizeof(abs_lib));
    if (strip_root_prefix(abs_file, abs_lib, rel_path, sizeof(rel_path))) {
        prefix = "lib";
    } else {
        // 2. ユーザーパッケージ (~/.japeng/packages) 判定
        bool matched_pkg = false;
        const char* home = getenv("HOME");
        if (home) {
            char raw_pkg[PATH_MAX], abs_pkg[PATH_MAX];
            snprintf(raw_pkg, sizeof(raw_pkg), "%s/.japeng/packages", home);
            normalize_abs_path(raw_pkg, abs_pkg, sizeof(abs_pkg));
            if (strip_root_prefix(abs_file, abs_pkg, rel_path, sizeof(rel_path))) {
                prefix = "pkg";
                matched_pkg = true;
            }
        }

        // 3. プロジェクトルート判定
        if (!matched_pkg) {
            char abs_proj[PATH_MAX];
            normalize_abs_path(project_dir ? project_dir : ".", abs_proj, sizeof(abs_proj));
            if (!strip_root_prefix(abs_file, abs_proj, rel_path, sizeof(rel_path))) {
                // ルート外の単発ファイル指定時はファイル名のみ使用
                const char* slash = strrchr(abs_file, '/');
                strncpy(rel_path, slash ? slash + 1 : abs_file, sizeof(rel_path) - 1);
            }
        }
    }

    // 末尾の拡張子 (.cd, .jcd 等) を除去
    char* dot = strrchr(rel_path, '.');
    if (dot) *dot = '\0';

    snprintf(out, max_len, "%s:%s::%s", prefix, rel_path, class_name);
}

bool resolve_bcd_path_from_canonical_id(const char* canonical_id, const char* project_dir,
                                        char* out_bcd_path, size_t max_len)
{
    if (!canonical_id) return false;

    // 1. builtin::<ClassName>
    if (strncmp(canonical_id, "builtin::", 9) == 0) {
        const char* class_name = canonical_id + 9;
        char builtin_dir[PATH_MAX];
        snprintf(builtin_dir, sizeof(builtin_dir), "%s/builtin", get_japeng_lib_dir());
        get_cache_filepath(builtin_dir, class_name, "bcd", out_bcd_path, max_len);
        return true;
    }

    // 2. <prefix>:<rel_path>::<ClassName> を分解
    const char* colon = strchr(canonical_id, ':');
    const char* sep   = strstr(canonical_id, "::");
    if (!colon || !sep || sep <= colon) return false;

    size_t prefix_len = (size_t)(colon - canonical_id);
    char prefix[16] = {0};
    if (prefix_len >= sizeof(prefix)) return false;
    memcpy(prefix, canonical_id, prefix_len);

    const char* rel_start = colon + 1;
    size_t rel_len = (size_t)(sep - rel_start);
    char rel_path[PATH_MAX] = {0};
    if (rel_len >= sizeof(rel_path)) return false;
    memcpy(rel_path, rel_start, rel_len);

    const char* class_name = sep + 2;

    // 基準ルートディレクトリの決定
    char root_dir[PATH_MAX] = {0};
    if (strcmp(prefix, "lib") == 0) {
        strncpy(root_dir, get_japeng_lib_dir(), sizeof(root_dir) - 1);
    } else if (strcmp(prefix, "pkg") == 0) {
        const char* home = getenv("HOME");
        if (!home) return false;
        snprintf(root_dir, sizeof(root_dir), "%s/.japeng/packages", home);
    } else if (strcmp(prefix, "proj") == 0) {
        strncpy(root_dir, project_dir ? project_dir : ".", sizeof(root_dir) - 1);
    } else {
        return false;
    }

    // rel_path からディレクトリ部分を取り出す
    char* last_slash = strrchr(rel_path, '/');
    char module_dir[PATH_MAX];
    if (last_slash) {
        *last_slash = '\0';
        snprintf(module_dir, sizeof(module_dir), "%s/%s", root_dir, rel_path);
    } else {
        strncpy(module_dir, root_dir, sizeof(module_dir) - 1);
    }

    get_cache_filepath(module_dir, class_name, "bcd", out_bcd_path, max_len);
    return true;
}
