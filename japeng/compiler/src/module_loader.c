#include "module_loader.h"
#include "preprocessor.h"
#include "builtin_convert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

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
    LanguageFrontend* fe = find_frontend_by_filename(cur_filename);
    char ext[16];
    snprintf(ext, sizeof(ext), ".%s", fe ? fe->ext_class : "cd");

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
    get_japeng_lib_dir_buf(lib_dir, sizeof(lib_dir));

    if (try_resolve_candidate(resolved, sizeof(resolved), lib_dir, mod_name_arg, file_name, ext)) return resolved;

    char sub_dir[1024];
    snprintf(sub_dir, sizeof(sub_dir), "%s/std", lib_dir);
    if (try_resolve_candidate(resolved, sizeof(resolved), sub_dir, mod_name_arg, file_name, ext)) return resolved;
    snprintf(sub_dir, sizeof(sub_dir), "%s/gui", lib_dir);
    if (try_resolve_candidate(resolved, sizeof(resolved), sub_dir, mod_name_arg, file_name, ext)) return resolved;

    return NULL;
}

static void register_module_alias_cb(const char* alias_str, const char* original_str, void* user_data)
{
    Compiler* c = (Compiler*)user_data;
    if (c->dict_aliases == NULL) c->dict_aliases = new_table(16);
    table_set(c->dict_aliases, intern_cstr(alias_str), make_obj((Obj*)intern_cstr(original_str)));
}

void load_module_dict(Compiler* c, const char* target_file, const char* cur_filename) 
{
    const char* cur_lang    = get_lang_code_from_filename(cur_filename);
    const char* target_lang = get_lang_code_from_filename(target_file);
    if (strcmp(cur_lang, target_lang) == 0) return;

    char dir_part[1024];
    strncpy(dir_part, target_file, sizeof(dir_part) - 1);
    dir_part[sizeof(dir_part) - 1] = '\0';
    char* slash = strrchr(dir_part, '/');
    const char* file_part = slash ? slash + 1 : target_file;
    if (slash) *slash = '\0';

    char base_name[128];
    strncpy(base_name, file_part, sizeof(base_name) - 1);
    base_name[sizeof(base_name) - 1] = '\0';
    char* dot = strrchr(base_name, '.');
    if (dot) *dot = '\0';

    char dict_path[1024];
    if (slash) {
        snprintf(dict_path, sizeof(dict_path), "%s/dict/%s.dict.%s", dir_part, base_name, cur_lang);
        if (parse_dict_file(dict_path, register_module_alias_cb, c)) return;
    }

    snprintf(dict_path, sizeof(dict_path), "%s/%s.dict.%s", dir_part, base_name, cur_lang);
    parse_dict_file(dict_path, register_module_alias_cb, c);
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
