#include "builtin_convert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#define MAX_DICT_ENTRIES 1024

static DictEntry builtin_entries[MAX_DICT_ENTRIES];
static int builtin_count = 0;

static DictEntry keyword_entries[MAX_DICT_ENTRIES];
static int keyword_count = 0;

static char loaded_lang_code[32] = {0};

const char* output_lang_to_code(OutputLang lang) {
    switch (lang) {
        case LANG_JA: return "ja";
        case LANG_EN:
        default:      return "en";
    }
}

void get_japeng_lib_dir_buf(char* out_buf, size_t buf_size) {
    char exe_path[1024] = {0};
#if defined(__APPLE__)
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) == 0) {
        char* slash = strrchr(exe_path, '/');
        if (slash) *slash = '\0';
        snprintf(out_buf, buf_size, "%s/../lib", exe_path);
        struct stat st;
        if (stat(out_buf, &st) == 0 && S_ISDIR(st.st_mode)) return;
    }
#endif
    snprintf(out_buf, buf_size, "./lib");
}

static char* trim_ws(char* str) {
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') str++;
    if (*str == '\0') return str;
    char* end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
    }
    return str;
}

bool parse_dict_file(const char* filepath, DictEntryCallback callback, void* user_data) {
    FILE* fp = fopen(filepath, "r");
    if (!fp) return false;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char* trimmed = trim_ws(line);
        if (trimmed[0] == '\0' || (trimmed[0] == '/' && trimmed[1] == '/') || trimmed[0] == '#') {
            continue;
        }
        char* colon = strchr(trimmed, ':');
        if (!colon) continue;

        *colon = '\0';
        char* key = trim_ws(trimmed);
        char* val = trim_ws(colon + 1);
        if (key[0] == '\0') continue;

        callback(key, val, user_data);
    }
    fclose(fp);
    return true;
}

typedef struct {
    DictEntry* table;
    int* count;
} DictArrayCtx;

static void append_dict_entry(const char* key, const char* val, void* user_data) {
    DictArrayCtx* ctx = (DictArrayCtx*)user_data;
    if (*(ctx->count) >= MAX_DICT_ENTRIES) return;
    ctx->table[*(ctx->count)].localized    = strdup(key);
    ctx->table[*(ctx->count)].canonical_en = strdup(val);
    (*(ctx->count))++;
}

static void clear_dict_table(DictEntry* table, int* count) {
    for (int i = 0; i < *count; i++) {
        free(table[i].localized);
        free(table[i].canonical_en);
    }
    *count = 0;
}

static void ensure_dicts_loaded(const char* lang_code) {
    if (!lang_code || lang_code[0] == '\0' || strcmp(lang_code, "en") == 0) return;
    if (strcmp(loaded_lang_code, lang_code) == 0) return;

    clear_dict_table(builtin_entries, &builtin_count);
    clear_dict_table(keyword_entries, &keyword_count);
    strncpy(loaded_lang_code, lang_code, sizeof(loaded_lang_code) - 1);

    char lib_dir[1024];
    get_japeng_lib_dir_buf(lib_dir, sizeof(lib_dir));

    // 1. 予約語・助詞辞書のロード (lib/lang/reserved.dict.<lang_code>)
    char reserved_path[1024];
    snprintf(reserved_path, sizeof(reserved_path), "%s/lang/reserved.dict.%s", lib_dir, lang_code);
    DictArrayCtx kw_ctx = { keyword_entries, &keyword_count };
    parse_dict_file(reserved_path, append_dict_entry, &kw_ctx);

    // 2. 組み込みクラス辞書のロード (lib/builtin/dict/*.dict.<lang_code>)
    char suffix[64];
    snprintf(suffix, sizeof(suffix), ".dict.%s", lang_code);
    size_t suffix_len = strlen(suffix);

    char builtin_dict_dir[1024];
    snprintf(builtin_dict_dir, sizeof(builtin_dict_dir), "%s/builtin/dict", lib_dir);
    DIR* dir = opendir(builtin_dict_dir);
    if (dir) {
        DictArrayCtx bi_ctx = { builtin_entries, &builtin_count };
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            size_t name_len = strlen(entry->d_name);
            if (name_len > suffix_len && strcmp(entry->d_name + name_len - suffix_len, suffix) == 0) {
                char full_path[1024];
                snprintf(full_path, sizeof(full_path), "%s/%s", builtin_dict_dir, entry->d_name);
                parse_dict_file(full_path, append_dict_entry, &bi_ctx);
            }
        }
        closedir(dir);
    }
}

const char* builtin_to_canonical(const char* name, size_t len, OutputLang lang) {
    const char* lang_code = output_lang_to_code(lang);
    if (strcmp(lang_code, "en") == 0) return NULL;
    ensure_dicts_loaded(lang_code);

    for (int i = 0; i < builtin_count; i++) {
        const char* loc = builtin_entries[i].localized;
        if (loc && strlen(loc) == len && strncmp(name, loc, len) == 0) {
            return builtin_entries[i].canonical_en;
        }
    }
    return NULL;
}

const DictEntry* builtin_match_prefix(const char* src, const char* lang_code, size_t* out_matched_len) {
    if (!lang_code || strcmp(lang_code, "en") == 0) return NULL;
    ensure_dicts_loaded(lang_code);

    const DictEntry* best = NULL;
    size_t max_len = 0;
    for (int i = 0; i < builtin_count; i++) {
        const char* loc = builtin_entries[i].localized;
        if (!loc) continue;
        size_t len = strlen(loc);
        if (strncmp(src, loc, len) == 0 && len > max_len) {
            max_len = len;
            best = &builtin_entries[i];
        }
    }
    if (best && out_matched_len) *out_matched_len = max_len;
    return best;
}

const char* builtin_from_canonical(const char* canonical_en, OutputLang lang) {
    if (!canonical_en) return "";
    const char* lang_code = output_lang_to_code(lang);
    if (strcmp(lang_code, "en") == 0) return canonical_en;
    ensure_dicts_loaded(lang_code);

    for (int i = 0; i < builtin_count; i++) {
        if (strcmp(canonical_en, builtin_entries[i].canonical_en) == 0) {
            return builtin_entries[i].localized;
        }
    }
    return canonical_en;
}

const DictEntry* keyword_match_prefix(const char* src, const char* lang_code, size_t* out_matched_len) {
    if (!lang_code || strcmp(lang_code, "en") == 0) return NULL;
    ensure_dicts_loaded(lang_code);

    const DictEntry* best = NULL;
    size_t max_len = 0;
    for (int i = 0; i < keyword_count; i++) {
        const char* loc = keyword_entries[i].localized;
        if (!loc) continue;
        size_t len = strlen(loc);
        if (strncmp(src, loc, len) == 0 && len > max_len) {
            max_len = len;
            best = &keyword_entries[i];
        }
    }
    if (best && out_matched_len) *out_matched_len = max_len;
    return best;
}
