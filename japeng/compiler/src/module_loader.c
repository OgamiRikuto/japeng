#include "module_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

// ファイルが存在し、ディレクトリでないかを確認
static bool file_exists(const char* path) 
{
    struct stat st;
    return (stat(path, &st) == 0 && !S_ISDIR(st.st_mode));
}

// 前後の空白・改行・タブを除去
static char* trim_whitespace(char* str) 
{
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') str++;
    if (*str == '\0') return str;
    char* end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
    }
    return str;
}

// 実行ファイル (bin/jpenc) の場所から ../lib/std を特定
static void get_std_lib_dir(char* out_buf, size_t buf_size) 
{
    char exe_path[1024] = {0};
#if defined(__APPLE__)
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) != 0) {
        snprintf(out_buf, buf_size, "./lib/std");
        return;
    }
#else
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        snprintf(out_buf, buf_size, "./lib/std");
        return;
    }
    exe_path[len] = '\0';
#endif
    char* slash = strrchr(exe_path, '/');
    if (slash) *slash = '\0'; // bin ディレクトリ

    // ../lib/std を構築
    snprintf(out_buf, buf_size, "%s/../lib/std", exe_path);
    struct stat st;
    if (stat(out_buf, &st) != 0 || !S_ISDIR(st.st_mode)) {
        // 開発フォルダ等で直接 ./jpenc を実行している場合のフォールバック
        snprintf(out_buf, buf_size, "./lib/std");
    }
}

// 拡張子から言語系統を判定 (.jje, .jcd -> "ja", その他 -> "en")
static const char* get_lang_code_from_ext(const char* filename) 
{
    if (!filename) return "en";
    const char* dot = strrchr(filename, '.');
    if (!dot) return "en";
    if (strcmp(dot, ".jje") == 0 || strcmp(dot, ".jcd") == 0) return "ja";
    return "en";
}

// ヘルパー: 指定ベースディレクトリ下で (1) ディレクトリ形式 (2) 単ファイル形式 を照合
static bool try_resolve_candidate(char* out, size_t out_size, 
                                  const char* base_dir, 
                                  const char* mod_path, 
                                  const char* mod_name, 
                                  const char* ext) 
{
    // パターン 1: ディレクトリ形式 (例: base/std/File/File.cd) ★第一優先
    snprintf(out, out_size, "%s/%s/%s%s", base_dir, mod_path, mod_name, ext);
    if (file_exists(out)) return true;
    snprintf(out, out_size, "%s/%s/%s.cd", base_dir, mod_path, mod_name);
    if (file_exists(out)) return true;

    // パターン 2: 単一ファイル形式 (例: base/std/File.cd) (フォールバック)
    snprintf(out, out_size, "%s/%s%s", base_dir, mod_path, ext);
    if (file_exists(out)) return true;
    snprintf(out, out_size, "%s/%s.cd", base_dir, mod_path);
    if (file_exists(out)) return true;

    return false;
}

// -------------------------------------------------------------
// 1. 3層パス探索 (ローカル -> ~/.japeng/packages/ -> lib/std/)
// -------------------------------------------------------------
const char* resolve_module_file_path(const char* mod_name_arg, const char* cur_filename) 
{
    static char resolved[1024];
    const char* cur_lang = get_lang_code_from_ext(cur_filename);
    const char* ext = (strcmp(cur_lang, "ja") == 0) ? ".jcd" : ".cd";

    // "std/File" -> mod_path: "std/File", file_name: "File"
    const char* slash = strrchr(mod_name_arg, '/');
    const char* file_name = slash ? slash + 1 : mod_name_arg;

    // 1. カレントディレクトリ / プロジェクト内
    if (try_resolve_candidate(resolved, sizeof(resolved), ".", mod_name_arg, file_name, ext)) {
        return resolved;
    }
    if (try_resolve_candidate(resolved, sizeof(resolved), "./modules", mod_name_arg, file_name, ext)) {
        return resolved;
    }

    // 2. ユーザー共通パッケージ (~/.japeng/packages/)
    const char* home = getenv("HOME");
    if (home) {
        char pkg_dir[1024];
        snprintf(pkg_dir, sizeof(pkg_dir), "%s/.japeng/packages", home);
        if (try_resolve_candidate(resolved, sizeof(resolved), pkg_dir, mod_name_arg, file_name, ext)) {
            return resolved;
        }
    }

    // 3. 公式標準ライブラリ
    char std_dir[1024];
    get_std_lib_dir(std_dir, sizeof(std_dir));
    
    // "std/File" が指定された場合は "std/" を除去して std_dir から引く
    const char* target_path = (strncmp(mod_name_arg, "std/", 4) == 0) ? mod_name_arg + 4 : mod_name_arg;
    const char* target_name = (strncmp(mod_name_arg, "std/", 4) == 0) ? file_name : mod_name_arg;

    if (try_resolve_candidate(resolved, sizeof(resolved), std_dir, target_path, target_name, ext)) {
        return resolved;
    }

    return NULL;
}

// -------------------------------------------------------------
// 2. 辞書ロード (言語が異なる場合のみ .dict.<lang> をパース)
// -------------------------------------------------------------
void load_module_dict(Compiler* c, const char* target_file, const char* cur_filename) 
{
    const char* cur_lang    = get_lang_code_from_ext(cur_filename);
    const char* target_lang = get_lang_code_from_ext(target_file);

    // 同一言語なら辞書引きはスキップ
    if (strcmp(cur_lang, target_lang) == 0) return;

    // ディレクトリとファイル名に分解
    char dir_part[1024];
    strncpy(dir_part, target_file, sizeof(dir_part) - 1);
    char* slash = strrchr(dir_part, '/');
    const char* file_part = slash ? slash + 1 : target_file;
    if (slash) *slash = '\0';

    // クラス名部分を取り出す
    char base_name[128];
    strncpy(base_name, file_part, sizeof(base_name) - 1);
    char* dot = strrchr(base_name, '.');
    if (dot) *dot = '\0';

    char dict_path[1024];
    FILE* fp = NULL;

    // パターン 1: dict/ サブディレクトリ内を探す (第一優先！)
    if (slash) {
        snprintf(dict_path, sizeof(dict_path), "%s/dict/%s.dict.%s", dir_part, base_name, cur_lang);
        fp = fopen(dict_path, "r");
    }

    // パターン 2: 直下を探す (フォールバック)
    if (!fp) {
        snprintf(dict_path, sizeof(dict_path), "%s/%s.dict.%s", dir_part, base_name, cur_lang);
        fp = fopen(dict_path, "r");
    }

    if (!fp) return; // 辞書がなくてもエラーにはせずスキップ

    if (c->dict_aliases == NULL) {
        c->dict_aliases = new_table(16);
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char* trimmed = trim_whitespace(line);
        // コメント (// または #) と空行をスキップ
        if (trimmed[0] == '\0' || 
            (trimmed[0] == '/' && trimmed[1] == '/') || 
            trimmed[0] == '#') {
            continue;
        }

        char* colon = strchr(trimmed, ':');
        if (!colon) continue;

        *colon = '\0';
        char* alias_str    = trim_whitespace(trimmed);
        char* original_str = trim_whitespace(colon + 1);

        Symbol* sym_alias    = intern_cstr(alias_str);
        Symbol* sym_original = intern_cstr(original_str);

        table_set(c->dict_aliases, sym_alias, make_obj((Obj*)sym_original));
    }
    fclose(fp);
}

// -------------------------------------------------------------
// 3. compile_import 本体
// -------------------------------------------------------------
void compile_import(Compiler* c, ASTNode* node) 
{
    if (!node || node->kind != AST_IMPORT) return;

    Symbol* path_sym    = node->import_stmt.path;
    ASTNode* name_node  = node->import_stmt.name;
    ASTNode* alias_node = node->import_stmt.alias;

    // 1. 探索先モジュールパス (search_path) の決定
    const char* search_path = NULL;
    if (path_sym != NULL) {
        search_path = path_sym->chars;
    } else if (name_node != NULL && name_node->kind == AST_IDENTIFIER) {
        search_path = name_node->identifier.name->chars;
    }

    if (search_path == NULL) {
        return; // パスもクラス名も取れなければ終了
    }

    // 2. モジュール内の対象クラス名 (target_class_sym) の決定
    Symbol* target_class_sym = NULL;
    if (name_node != NULL && name_node->kind == AST_IDENTIFIER) {
        target_class_sym = name_node->identifier.name;
    } else {
        const char* slash = strrchr(search_path, '/');
        const char* fname = slash ? slash + 1 : search_path;
        target_class_sym = intern_cstr(fname);
    }

    // 3. スコープ内でバインドする名前 (bind_name) の決定
    Symbol* bind_name = NULL;
    if (alias_node != NULL && alias_node->kind == AST_IDENTIFIER) {
        bind_name = alias_node->identifier.name; // as で指定されたエイリアス
    } else {
        bind_name = target_class_sym;           // alias が無ければ元のクラス名
    }

    // 4. ファイル探索 (3層パス & ディレクトリ挟み込み対応)
    const char* file_path = resolve_module_file_path(search_path, c->filename);
    if (!file_path) {
        error_compile(node, ERR_CLASS_NOT_REGISTERED, search_path);
        return;
    }

    // 5. 辞書ロード (言語が違えば .dict.<lang> を読み込む)
    load_module_dict(c, file_path, c->filename);

    // 6. クラスのコンパイルとロード
    ClassDef* klass = resolve_class_from_file(c, file_path);
    if (!klass) return;

    // 7. imported_classes テーブルに登録
    if (c->imported_classes == NULL) {
        c->imported_classes = new_table(8);
    }
    table_set(c->imported_classes, bind_name, make_obj((Obj*)klass));
}
