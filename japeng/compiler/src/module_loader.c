#include "module_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

// ファイルが存在し、ディレクトリでないかを確認
static bool file_exists(const char* path) 
{
    struct stat st;
    return (stat(path, &st) == 0 && !S_ISDIR(st.st_mode));
}

// ディレクトリが存在するかを確認
static bool dir_exists(const char* path) 
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
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

// 実行ファイル (bin/jpenc) の場所から ../lib を特定
static void get_std_lib_dir(char* out_buf, size_t buf_size) 
{
    char exe_path[1024] = {0};
#if defined(__APPLE__)
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) != 0) {
        snprintf(out_buf, buf_size, "./lib");
        return;
    }
#else
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        snprintf(out_buf, buf_size, "./lib");
        return;
    }
    exe_path[len] = '\0';
#endif
    char* slash = strrchr(exe_path, '/');
    if (slash) *slash = '\0';

    snprintf(out_buf, buf_size, "%s/../lib", exe_path);
    struct stat st;
    if (stat(out_buf, &st) != 0 || !S_ISDIR(st.st_mode)) {
        snprintf(out_buf, buf_size, "./lib");
    }
}

static const char* get_lang_code_from_ext(const char* filename) 
{
    if (!filename) return "en";
    const char* dot = strrchr(filename, '.');
    if (!dot) return "en";
    if (strcmp(dot, ".jje") == 0 || strcmp(dot, ".jcd") == 0) return "ja";
    return "en";
}

static bool try_resolve_candidate(char* out, size_t out_size, 
                                  const char* base_dir, 
                                  const char* mod_path, 
                                  const char* mod_name, 
                                  const char* ext) 
{
    // パターン 1: ディレクトリ形式
    snprintf(out, out_size, "%s/%s/%s%s", base_dir, mod_path, mod_name, ext);
    if (file_exists(out)) return true;
    snprintf(out, out_size, "%s/%s/%s.cd", base_dir, mod_path, mod_name);
    if (file_exists(out)) return true;

    // パターン 2: 単一ファイル形式
    snprintf(out, out_size, "%s/%s%s", base_dir, mod_path, ext);
    if (file_exists(out)) return true;
    snprintf(out, out_size, "%s/%s.cd", base_dir, mod_path);
    if (file_exists(out)) return true;

    // パターン 3: ディレクトリそのものが存在する場合
    snprintf(out, out_size, "%s/%s", base_dir, mod_path);
    if (dir_exists(out)) return true;

    return false;
}

const char* resolve_module_file_path(const char* mod_name_arg, const char* cur_filename) 
{
    static char resolved[1024];
    const char* cur_lang = get_lang_code_from_ext(cur_filename);
    const char* ext = (strcmp(cur_lang, "ja") == 0) ? ".jcd" : ".cd";

    const char* slash = strrchr(mod_name_arg, '/');
    const char* file_name = slash ? slash + 1 : mod_name_arg;

    // 1. カレントディレクトリ / プロジェクト内
    if (try_resolve_candidate(resolved, sizeof(resolved), ".", mod_name_arg, file_name, ext)) return resolved;
    if (try_resolve_candidate(resolved, sizeof(resolved), "./modules", mod_name_arg, file_name, ext)) return resolved;

    // 2. ユーザー共通パッケージ
    const char* home = getenv("HOME");
    if (home) {
        char pkg_dir[1024];
        snprintf(pkg_dir, sizeof(pkg_dir), "%s/.japeng/packages", home);
        if (try_resolve_candidate(resolved, sizeof(resolved), pkg_dir, mod_name_arg, file_name, ext)) return resolved;
    }

    // 3. 標準ライブラリ
    char lib_dir[1024];
    get_std_lib_dir(lib_dir, sizeof(lib_dir));

    if (try_resolve_candidate(resolved, sizeof(resolved), lib_dir, mod_name_arg, file_name, ext)) return resolved;
    
    char sub_dir[1024];
    snprintf(sub_dir, sizeof(sub_dir), "%s/std", lib_dir);
    if (try_resolve_candidate(resolved, sizeof(resolved), sub_dir, mod_name_arg, file_name, ext)) return resolved;
    snprintf(sub_dir, sizeof(sub_dir), "%s/gui", lib_dir);
    if (try_resolve_candidate(resolved, sizeof(resolved), sub_dir, mod_name_arg, file_name, ext)) return resolved;

    return NULL;
}

void load_module_dict(Compiler* c, const char* target_file, const char* cur_filename) 
{
    const char* cur_lang    = get_lang_code_from_ext(cur_filename);
    const char* target_lang = get_lang_code_from_ext(target_file);
    if (strcmp(cur_lang, target_lang) == 0) return;

    char dir_part[1024];
    strncpy(dir_part, target_file, sizeof(dir_part) - 1);
    char* slash = strrchr(dir_part, '/');
    const char* file_part = slash ? slash + 1 : target_file;
    if (slash) *slash = '\0';

    char base_name[128];
    strncpy(base_name, file_part, sizeof(base_name) - 1);
    char* dot = strrchr(base_name, '.');
    if (dot) *dot = '\0';

    char dict_path[1024];
    FILE* fp = NULL;

    if (slash) {
        snprintf(dict_path, sizeof(dict_path), "%s/dict/%s.dict.%s", dir_part, base_name, cur_lang);
        fp = fopen(dict_path, "r");
    }
    if (!fp) {
        snprintf(dict_path, sizeof(dict_path), "%s/%s.dict.%s", dir_part, base_name, cur_lang);
        fp = fopen(dict_path, "r");
    }
    if (!fp) return;

    if (c->dict_aliases == NULL) c->dict_aliases = new_table(16);

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char* trimmed = trim_whitespace(line);
        if (trimmed[0] == '\0' || (trimmed[0] == '/' && trimmed[1] == '/') || trimmed[0] == '#') continue;

        char* colon = strchr(trimmed, ':');
        if (!colon) continue;

        *colon = '\0';
        char* alias_str    = trim_whitespace(trimmed);
        char* original_str = trim_whitespace(colon + 1);

        table_set(c->dict_aliases, intern_cstr(alias_str), make_obj((Obj*)intern_cstr(original_str)));
    }
    fclose(fp);
}

// ディレクトリ内のすべての .bcd / .cd を一括インポートするヘルパー
static void import_all_from_dir(Compiler* c, const char* dir_path) 
{
    char cache_dir[1024];
    snprintf(cache_dir, sizeof(cache_dir), "%s/.je_cache", dir_path);
    const char* scan_target = dir_exists(cache_dir) ? cache_dir : dir_path;

    DIR* dir = opendir(scan_target);
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        const char* dot = strrchr(entry->d_name, '.');
        if (!dot) continue;

        // .bcd または .cd を対象にする
        if (strcmp(dot, ".bcd") == 0 || strcmp(dot, ".cd") == 0) {
            char file_buf[1024];
            snprintf(file_buf, sizeof(file_buf), "%s/%s", scan_target, entry->d_name);

            char class_name_buf[128];
            strncpy(class_name_buf, entry->d_name, sizeof(class_name_buf) - 1);
            char* cdot = strrchr(class_name_buf, '.');
            if (cdot) *cdot = '\0';

            ClassDef* klass = resolve_class_from_file(c, file_buf);
            if (klass) {
                if (c->imported_classes == NULL) c->imported_classes = new_table(16);
                table_set(c->imported_classes, intern_cstr(class_name_buf), make_obj((Obj*)klass));
            }
        }
    }
    closedir(dir);
}

void compile_import(Compiler* c, ASTNode* node) 
{
    if (!node || node->kind != AST_IMPORT) return;

    Symbol* path_sym    = node->import_stmt.path;
    ASTNode* name_node  = node->import_stmt.name;
    ASTNode* alias_node = node->import_stmt.alias;

    // 1. from "path" import Name の場合の結合パス構築
    char combined_path[1024] = {0};
    if (path_sym != NULL && name_node != NULL && name_node->kind == AST_IDENTIFIER) {
        // 例: from "gui" import Window -> "gui/Window"
        snprintf(combined_path, sizeof(combined_path), "%s/%s", path_sym->chars, name_node->identifier.name->chars);
    } else if (path_sym != NULL) {
        strncpy(combined_path, path_sym->chars, sizeof(combined_path) - 1);
    } else if (name_node != NULL && name_node->kind == AST_IDENTIFIER) {
        strncpy(combined_path, name_node->identifier.name->chars, sizeof(combined_path) - 1);
    }

    if (combined_path[0] == '\0') return;

    // 2. ファイルまたはディレクトリを探索
    const char* resolved_path = resolve_module_file_path(combined_path, c->filename);
    if (!resolved_path) {
        error_compile(node, ERR_CLASS_NOT_REGISTERED, combined_path);
        return;
    }

    // 3. ディレクトリなら中身を一括ロードして終了
    if (dir_exists(resolved_path)) {
        import_all_from_dir(c, resolved_path);
        return;
    }

    // 4. 単一ファイルの場合のバインド名決定
    Symbol* bind_name = NULL;
    if (alias_node != NULL && alias_node->kind == AST_IDENTIFIER) {
        bind_name = alias_node->identifier.name;
    } else if (name_node != NULL && name_node->kind == AST_IDENTIFIER) {
        bind_name = name_node->identifier.name;
    } else {
        const char* slash = strrchr(combined_path, '/');
        bind_name = intern_cstr(slash ? slash + 1 : combined_path);
    }

    // 5. 辞書とクラスをロード
    load_module_dict(c, resolved_path, c->filename);
    ClassDef* klass = resolve_class_from_file(c, resolved_path);
    if (!klass) return;

    if (c->imported_classes == NULL) c->imported_classes = new_table(8);
    table_set(c->imported_classes, bind_name, make_obj((Obj*)klass));
}
