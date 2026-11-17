#include "module_loader.h"
#include "preprocessor.h"
#include "builtin_convert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>

static bool file_exists(const char* path)
{
    struct stat st;
    return (stat(path, &st) == 0 && !S_ISDIR(st.st_mode));
}

static bool dir_exists(const char* path)
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static Symbol* extract_import_symbol(ASTNode* node)
{
    if (node == NULL) return NULL;
    if (node->kind == AST_IDENTIFIER) return node->identifier.name;
    if (node->kind == AST_CLASS && node->klass.classname != NULL) {
        return node->klass.classname->identifier.name;
    }
    return NULL;
}

// -----------------------------------------------------------------------------
// 1. ClassBinding（ファイル単位のローカル名前空間）管理
// -----------------------------------------------------------------------------

ClassBinding* find_class_binding(Compiler* c, Symbol* local_name)
{
    if (c == NULL || local_name == NULL) return NULL;
    for (Compiler* comp = c; comp != NULL; comp = comp->enclosing) {
        if (comp->class_bindings != NULL) {
            Value val;
            if (table_get(comp->class_bindings, local_name, &val)) {
                return (ClassBinding*)as_obj(val);
            }
        }
    }
    return NULL;
}

ClassBinding* register_class_binding(Compiler* c, Symbol* local_name, Symbol* canonical_id, ClassDef* klass)
{
    if (c == NULL || local_name == NULL || klass == NULL) return NULL;

    if (c->class_bindings == NULL) {
        c->class_bindings = new_table(16);
    }

    Value existing_val;
    if (table_get(c->class_bindings, local_name, &existing_val)) {
        ClassBinding* existing = (ClassBinding*)as_obj(existing_val);
        // すでに同じ戸籍IDでバインド済みならそのまま返す
        if (existing != NULL && existing->canonical_id == canonical_id) {
            return existing;
        }
    }

    ClassBinding* binding = (ClassBinding*)malloc(sizeof(ClassBinding));
    binding->local_name   = local_name;
    binding->canonical_id = canonical_id;
    binding->klass        = klass;

    table_set(c->class_bindings, local_name, make_obj((Obj*)binding));
    return binding;
}

// -----------------------------------------------------------------------------
// 2. 辞書ロード (.dict.<lang>)
// -----------------------------------------------------------------------------

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

    char dir_part[PATH_MAX];
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

    char dict_path[PATH_MAX];
    if (slash) {
        snprintf(dict_path, sizeof(dict_path), "%s/dict/%s.dict.%s", dir_part, base_name, cur_lang);
        if (parse_dict_file(dict_path, register_module_alias_cb, c)) return;
    }

    snprintf(dict_path, sizeof(dict_path), "%s/%s.dict.%s", dir_part, base_name, cur_lang);
    parse_dict_file(dict_path, register_module_alias_cb, c);
}

static bool try_resolve_candidate(char* out, size_t out_size,
                                  const char* base_dir,
                                  const char* mod_path,
                                  const char* mod_name,
                                  const char* ext)
{
    snprintf(out, out_size, "%s/%s/%s%s", base_dir, mod_path, mod_name, ext);
    if (file_exists(out)) return true;
    snprintf(out, out_size, "%s/%s/%s.cd", base_dir, mod_path, mod_name);
    if (file_exists(out)) return true;

    snprintf(out, out_size, "%s/%s%s", base_dir, mod_path, ext);
    if (file_exists(out)) return true;
    snprintf(out, out_size, "%s/%s.cd", base_dir, mod_path);
    if (file_exists(out)) return true;

    snprintf(out, out_size, "%s/%s", base_dir, mod_path);
    if (dir_exists(out)) return true;

    return false;
}

const char* resolve_module_file_path(const char* mod_name_arg, const char* cur_filename)
{
    static char resolved[PATH_MAX];
    LanguageFrontend* fe = find_frontend_by_filename(cur_filename);
    char ext[16];
    snprintf(ext, sizeof(ext), ".%s", fe ? fe->ext_class : "cd");

    const char* slash = strrchr(mod_name_arg, '/');
    const char* file_name = slash ? slash + 1 : mod_name_arg;

    if (try_resolve_candidate(resolved, sizeof(resolved), ".", mod_name_arg, file_name, ext)) return resolved;
    if (try_resolve_candidate(resolved, sizeof(resolved), "./modules", mod_name_arg, file_name, ext)) return resolved;

    const char* home = getenv("HOME");
    if (home) {
        char pkg_dir[PATH_MAX];
        snprintf(pkg_dir, sizeof(pkg_dir), "%s/.japeng/packages", home);
        if (try_resolve_candidate(resolved, sizeof(resolved), pkg_dir, mod_name_arg, file_name, ext)) return resolved;
    }

    char lib_dir[PATH_MAX];
    get_japeng_lib_dir_buf(lib_dir, sizeof(lib_dir));

    if (try_resolve_candidate(resolved, sizeof(resolved), lib_dir, mod_name_arg, file_name, ext)) return resolved;

    char sub_dir[PATH_MAX];
    snprintf(sub_dir, sizeof(sub_dir), "%s/std", lib_dir);
    if (try_resolve_candidate(resolved, sizeof(resolved), sub_dir, mod_name_arg, file_name, ext)) return resolved;
    snprintf(sub_dir, sizeof(sub_dir), "%s/gui", lib_dir);
    if (try_resolve_candidate(resolved, sizeof(resolved), sub_dir, mod_name_arg, file_name, ext)) return resolved;

    return NULL;
}

// -----------------------------------------------------------------------------
// 3. モジュールディレクトリ解決
//    探索順: project_dir -> <project_dir>/modules -> ~/.japeng/packages -> lib/ (および std/, gui/)
// -----------------------------------------------------------------------------

static bool resolve_module_directory(Compiler* c, const char* rel_dir, char* out_dir, size_t max_len)
{
    const char* proj = (c && c->project_dir) ? c->project_dir : ".";

    // 1. プロジェクトルート配下
    snprintf(out_dir, max_len, "%s/%s", proj, rel_dir);
    if (dir_exists(out_dir)) return true;

    snprintf(out_dir, max_len, "%s/modules/%s", proj, rel_dir);
    if (dir_exists(out_dir)) return true;

    // 2. ユーザー共通パッケージ (~/.japeng/packages)
    const char* home = getenv("HOME");
    if (home) {
        snprintf(out_dir, max_len, "%s/.japeng/packages/%s", home, rel_dir);
        if (dir_exists(out_dir)) return true;
    }

    // 3. 標準ライブラリ (lib/, lib/std/, lib/gui/)
    char lib_dir[PATH_MAX];
    get_japeng_lib_dir_buf(lib_dir, sizeof(lib_dir));

    snprintf(out_dir, max_len, "%s/%s", lib_dir, rel_dir);
    if (dir_exists(out_dir)) return true;

    snprintf(out_dir, max_len, "%s/std/%s", lib_dir, rel_dir);
    if (dir_exists(out_dir)) return true;

    snprintf(out_dir, max_len, "%s/gui/%s", lib_dir, rel_dir);
    if (dir_exists(out_dir)) return true;

    return false;
}

// 指定ディレクトリから単一クラスファイルをロードし、ClassBinding に登録する
static bool load_single_class_from_dir(Compiler* c, const char* dir_path,
                                       Symbol* class_sym, Symbol* bind_name)
{
    LanguageFrontend* fe = find_frontend_by_filename(c->filename);
    const char* lang_ext = fe ? fe->ext_class : "cd";

    char file_path[PATH_MAX] = {0};
    snprintf(file_path, sizeof(file_path), "%s/%s.%s", dir_path, class_sym->chars, lang_ext);
    if (!file_exists(file_path)) {
        snprintf(file_path, sizeof(file_path), "%s/%s.cd", dir_path, class_sym->chars);
    }
    if (!file_exists(file_path)) {
        snprintf(file_path, sizeof(file_path), "%s/%s.jcd", dir_path, class_sym->chars);
    }
    if (!file_exists(file_path)) {
        return false;
    }

    load_module_dict(c, file_path, c->filename);
    ClassDef* klass = resolve_class_from_file(c, file_path);
    if (!klass) return false;

    char cid_buf[PATH_MAX];
    make_canonical_id(c->project_dir, file_path, klass->name->chars, is_builtin(klass->name), cid_buf, sizeof(cid_buf));
    Symbol* cid_sym = intern_cstr(cid_buf);
    if (klass->canonical_id == NULL) klass->canonical_id = cid_sym;

    register_class_binding(c, bind_name, cid_sym, klass);
    return true;
}

// 指定ディレクトリ内の全クラスを一括インポートする
// 指定ディレクトリ内の全クラスを一括インポートする
static bool import_all_classes_in_dir(Compiler* c, const char* dir_path,
                                      const char* exclude_filename, bool overwrite_existing)
{
    DIR* dir = opendir(dir_path);
    if (!dir) return false;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        // 自分自身のファイルはスキップ（無限再帰防止）
        if (exclude_filename != NULL && strcmp(entry->d_name, exclude_filename) == 0) {
            continue;
        }

        const char* dot = strrchr(entry->d_name, '.');
        if (!dot) continue;

        if (strcmp(dot, ".cd") == 0 || strcmp(dot, ".jcd") == 0) {
            char file_buf[PATH_MAX];
            snprintf(file_buf, sizeof(file_buf), "%s/%s", dir_path, entry->d_name);

            char class_name_buf[128];
            strncpy(class_name_buf, entry->d_name, sizeof(class_name_buf) - 1);
            class_name_buf[sizeof(class_name_buf) - 1] = '\0';
            char* cdot = strrchr(class_name_buf, '.');
            if (cdot) *cdot = '\0';

            Symbol* local_sym = intern_cstr(class_name_buf);
            if (!overwrite_existing && find_class_binding(c, local_sym) != NULL) {
                continue;
            }

            load_module_dict(c, file_buf, c->filename);
            ClassDef* klass = resolve_class_from_file(c, file_buf);
            if (klass != NULL) {
                char cid_buf[PATH_MAX];
                make_canonical_id(c->project_dir, file_buf, klass->name->chars, is_builtin(klass->name), cid_buf, sizeof(cid_buf));
                Symbol* cid_sym = intern_cstr(cid_buf);
                if (klass->canonical_id == NULL) klass->canonical_id = cid_sym;

                register_class_binding(c, local_sym, cid_sym, klass);
            }
        }
    }
    closedir(dir);
    return true;
}

// -----------------------------------------------------------------------------
// 4. 同一ディレクトリ内のクラスを暗黙バインドする
// -----------------------------------------------------------------------------

void import_same_directory_classes(Compiler* c, const char* cur_filepath)
{
    if (c == NULL || cur_filepath == NULL || cur_filepath[0] == '\0') return;

    char dir_path[PATH_MAX];
    const char* slash = strrchr(cur_filepath, '/');
    const char* file_only = slash ? slash + 1 : cur_filepath;

    if (slash) {
        size_t len = (size_t)(slash - cur_filepath);
        if (len >= sizeof(dir_path)) len = sizeof(dir_path) - 1;
        memcpy(dir_path, cur_filepath, len);
        dir_path[len] = '\0';
    } else {
        strncpy(dir_path, c->project_dir ? c->project_dir : ".", sizeof(dir_path) - 1);
        dir_path[sizeof(dir_path) - 1] = '\0';
    }

    if (!dir_exists(dir_path)) return;
    import_all_classes_in_dir(c, dir_path, file_only, false);
}

// -----------------------------------------------------------------------------
// 5. compile_import 本体（3つのルール）
// -----------------------------------------------------------------------------

void compile_import(Compiler* c, ASTNode* node)
{
    if (!node || node->kind != AST_IMPORT) return;

    Symbol* path_sym  = node->import_stmt.path;
    Symbol* name_sym  = extract_import_symbol(node->import_stmt.name);
    Symbol* alias_sym = extract_import_symbol(node->import_stmt.alias);

    // ルール 1: from "ディレクトリパス" import クラス名 (as 別名);
    if (path_sym != NULL && name_sym != NULL) {
        char resolved_dir[PATH_MAX];
        if (!resolve_module_directory(c, path_sym->chars, resolved_dir, sizeof(resolved_dir))) {
            error_compile(node, ERR_CLASS_NOT_REGISTERED, path_sym->chars);
            return;
        }

        Symbol* bind_name = alias_sym ? alias_sym : name_sym;
        if (!load_single_class_from_dir(c, resolved_dir, name_sym, bind_name)) {
            error_compile(node, ERR_CLASS_NOT_REGISTERED, name_sym->chars);
        }
        return;
    }

    // ルール 2: import "ディレクトリパス"; （一括インポート専用・as禁止）
    if (path_sym != NULL && name_sym == NULL) {
        if (alias_sym != NULL) {
            error_compile(node, ERR_CLASS_NOT_REGISTERED, path_sym->chars);
            return;
        }

        char resolved_dir[PATH_MAX];
        if (!resolve_module_directory(c, path_sym->chars, resolved_dir, sizeof(resolved_dir))) {
            error_compile(node, ERR_CLASS_NOT_REGISTERED, path_sym->chars);
            return;
        }

        import_all_classes_in_dir(c, resolved_dir, NULL, true);
        return;
    }

    // ルール 3: import クラス名 (as 別名); （lib/std/<クラス名>/<クラス名>.cd 専用ショートカット）
    if (path_sym == NULL && name_sym != NULL) {
        char lib_dir[PATH_MAX];
        get_japeng_lib_dir_buf(lib_dir, sizeof(lib_dir));

        char std_mod_dir[PATH_MAX];
        snprintf(std_mod_dir, sizeof(std_mod_dir), "%s/std/%s", lib_dir, name_sym->chars);

        Symbol* bind_name = alias_sym ? alias_sym : name_sym;
        if (!dir_exists(std_mod_dir) || !load_single_class_from_dir(c, std_mod_dir, name_sym, bind_name)) {
            error_compile(node, ERR_CLASS_NOT_REGISTERED, name_sym->chars);
        }
        return;
    }
}
