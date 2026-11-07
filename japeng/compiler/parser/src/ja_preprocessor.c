#include "preprocessor.h"
#include "builtin_convert.h"
#include "utf8_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#define MAX_IDENTS 2048

typedef struct {
    char original[256];
    char placeholder[32];
} IdentMapping;

static IdentMapping ident_map[MAX_IDENTS];
static int ident_count = 0;
static int type_count = 0;

// =============================================================================
// 日本語 SourceMap 機構
// =============================================================================
typedef struct {
    int orig_line;
    int orig_col;
} SourceLoc;

typedef struct {
    int cap;
    int count;
    SourceLoc* cols;
} LineMap;

static LineMap* g_line_maps = NULL;
static int g_line_map_cap = 0;
static int g_line_map_count = 0;

static void free_ja_source_map(void) {
    if (g_line_maps) {
        for (int i = 0; i < g_line_map_count; i++) {
            if (g_line_maps[i].cols) free(g_line_maps[i].cols);
        }
        free(g_line_maps);
        g_line_maps = NULL;
    }
    g_line_map_cap = 0;
    g_line_map_count = 0;
}

static void record_ja_map(int clean_line, int clean_col, int orig_line, int orig_col) {
    if (clean_line > g_line_map_cap) {
        int old_cap = g_line_map_cap;
        g_line_map_cap = (clean_line + 64) * 2;
        g_line_maps = (LineMap*)realloc(g_line_maps, sizeof(LineMap) * g_line_map_cap);
        for (int i = old_cap; i < g_line_map_cap; i++) {
            g_line_maps[i].cap = 0;
            g_line_maps[i].count = 0;
            g_line_maps[i].cols = NULL;
        }
    }
    if (clean_line > g_line_map_count) {
        g_line_map_count = clean_line;
    }

    LineMap* lm = &g_line_maps[clean_line - 1];
    if (clean_col > lm->cap) {
        lm->cap = (clean_col + 64) * 2;
        lm->cols = (SourceLoc*)realloc(lm->cols, sizeof(SourceLoc) * lm->cap);
    }
    while (lm->count < clean_col) {
        lm->cols[lm->count].orig_line = orig_line;
        lm->cols[lm->count].orig_col = orig_col;
        lm->count++;
    }
    lm->cols[clean_col - 1].orig_line = orig_line;
    lm->cols[clean_col - 1].orig_col = orig_col;
}

static void ja_map_loc(int clean_line, int clean_col, int* out_orig_line, int* out_orig_col) {
    if (clean_line > 0 && clean_line <= g_line_map_count) {
        LineMap* lm = &g_line_maps[clean_line - 1];
        if (clean_col > 0 && clean_col <= lm->count) {
            *out_orig_line = lm->cols[clean_col - 1].orig_line;
            *out_orig_col  = lm->cols[clean_col - 1].orig_col;
            return;
        } else if (lm->count > 0) {
            *out_orig_line = lm->cols[lm->count - 1].orig_line;
            *out_orig_col  = lm->cols[lm->count - 1].orig_col;
            return;
        }
    }
    *out_orig_line = clean_line;
    *out_orig_col  = clean_col;
}

static void ja_reset(void) {
    ident_count = 0;
    type_count = 0;
    free_ja_source_map();
}

static inline int get_char_width(const char* p) {
    unsigned char c = (unsigned char)*p;
    if (c == '\t') return 4;
    if (c < 0x80) return 1;
    return 2;
}

static inline void emit_char(char* out, int* w, char c, int* clean_line, int* clean_col, int orig_line, int orig_col) {
    out[(*w)++] = c;
    record_ja_map(*clean_line, *clean_col, orig_line, orig_col);
    if (c == '\n') {
        (*clean_line)++;
        *clean_col = 1;
    } else {
        (*clean_col)++;
    }
}

static inline void emit_token_str(char* out, int* w, const char* str, int* clean_line, int* clean_col, int orig_line, int orig_start_col, int orig_end_col) {
    size_t len = strlen(str);
    for (size_t i = 0; i < len; i++) {
        int col = (i == len - 1 && len > 1) ? orig_end_col : orig_start_col;
        emit_char(out, w, str[i], clean_line, clean_col, orig_line, col);
    }
}

static inline bool is_blank_space(const char* p) {
    if (*p == ' ' || *p == '\t') return true;
    if ((unsigned char)p[0] == 0xE3 && (unsigned char)p[1] == 0x80 && (unsigned char)p[2] == 0x80) return true;
    return false;
}

static inline bool is_after_indent(const char* src, int r) {
    int p = r - 1;
    while (p >= 0 && (src[p] == ' ' || src[p] == '\t' ||
           ((unsigned char)src[p] == 0x80 && p >= 2 && (unsigned char)src[p-2] == 0xE3))) {
        if ((unsigned char)src[p] == 0x80) p -= 3;
        else p--;
    }
    if (p < 0 || src[p] == '\n' || src[p] == '\r') return true;
    return false;
}

static inline bool is_delim(const char* p) {
    if (*p == '\0') return true;
    char c = *p;
    if (isspace((unsigned char)c) || c == '(' || c == ')' || c == '[' || c == ']' ||
        c == '{' || c == '}' || c == ',' || c == '.' || c == ';' || c == ':' || c == '"') {
        return true;
    }
    if ((unsigned char)p[0] == 0xE3 && (unsigned char)p[1] == 0x80 && 
        ((unsigned char)p[2] == 0x80 || (unsigned char)p[2] == 0x81 || (unsigned char)p[2] == 0x82)) return true;
    if ((unsigned char)p[0] == 0xE3 && (unsigned char)p[1] == 0x83 && (unsigned char)p[2] == 0xBB) return true;
    if ((unsigned char)p[0] == 0xE3 && (unsigned char)p[1] == 0x80 && 
        ((unsigned char)p[2] >= 0x8C && (unsigned char)p[2] <= 0x8F)) return true;
    return false;
}

// 助詞判定 (テーブル駆動)
static const char* const ja_particle_table[] = {
    // 2文字助詞 (6バイト) - 最長一致のため先頭
    "から", "より", "なら", "の間", "まで",
    // 1文字助詞 (3バイト)
    "が", "に", "へ", "で", "を", "の",
    NULL // 番兵
};

static inline int get_particle_len(const char* p) {
    for (int i = 0; ja_particle_table[i] != NULL; i++) {
        size_t len = strlen(ja_particle_table[i]);
        if (strncmp(p, ja_particle_table[i], len) == 0) {
            return (int)len;
        }
    }
    return 0;
}

static inline bool is_word_boundary(const char* p) {
    if (is_delim(p)) return true;
    if (get_particle_len(p) > 0) return true;
    if (strncmp(p, "は", 3) == 0) return true;
    return false;
}

static const char* ja_lookup_symbol(const char* placeholder) {
    for (int i = 0; i < ident_count; i++) {
        if (strcmp(ident_map[i].placeholder, placeholder) == 0) {
            return ident_map[i].original;
        }
    }
    return NULL;
}

static const char* register_identifier(const char* name, int len) {
    if (len <= 0 || len >= 256) return NULL;
    char temp[256];
    memcpy(temp, name, len);
    temp[len] = '\0';

    for (int i = 0; i < ident_count; i++) {
        if (strcmp(ident_map[i].original, temp) == 0) return ident_map[i].placeholder;
    }
    if (ident_count >= MAX_IDENTS) exit(EXIT_FAILURE);

    strncpy(ident_map[ident_count].original, temp, sizeof(ident_map[ident_count].original) - 1);
    snprintf(ident_map[ident_count].placeholder, sizeof(ident_map[ident_count].placeholder), "__id_%d__", ident_count);
    ident_count++;
    return ident_map[ident_count - 1].placeholder;
}

static const char* register_type(const char* name, int len) {
    if (len <= 0 || len >= 256) return NULL;
    char temp[256];
    memcpy(temp, name, len);
    temp[len] = '\0';

    for (int i = 0; i < ident_count; i++) {
        if (strcmp(ident_map[i].original, temp) == 0) return ident_map[i].placeholder;
    }
    if (ident_count >= MAX_IDENTS) exit(EXIT_FAILURE);

    strncpy(ident_map[ident_count].original, temp, sizeof(ident_map[ident_count].original) - 1);
    snprintf(ident_map[ident_count].placeholder, sizeof(ident_map[ident_count].placeholder), "CLASSTYPE_%d", type_count++);
    ident_count++;
    return ident_map[ident_count - 1].placeholder;
}

static void scan_declarations(const char* src) {
    int i = 0;
    int str_stack[32];
    int str_stack_top = 0;

    while (src[i] != '\0') {
        bool in_string = (str_stack_top > 0 && str_stack[str_stack_top - 1] == 1);
        if (in_string) {
            if (src[i] == '\\' && src[i + 1] != '\0') { i += 2; continue; }
            if (src[i] == '"') { i++; str_stack_top--; continue; }
            if (src[i] == '{') { i++; str_stack[str_stack_top++] = 0; continue; }
            i++;
            continue;
        }

        if (src[i] == '/' && src[i + 1] == '/') {
            while (src[i] != '\n' && src[i] != '\0') i++;
            continue;
        }

        if (src[i] == '"') {
            i++;
            str_stack[str_stack_top++] = 1;
            continue;
        }

        if (src[i] == '}' && str_stack_top > 0 && str_stack[str_stack_top - 1] == 0) {
            i++;
            str_stack_top--;
            continue;
        }

        size_t kw_len = 0;
        const KeywordDef* kw = keyword_match_prefix(&src[i], LANG_JA, &kw_len);
        if (kw && (strcmp(kw->replacement, "class ") == 0 || strcmp(kw->replacement, "interface") == 0)) {
            int c_start = i + kw_len;
            while (src[c_start] != '\0' && (src[c_start] == ' ' || src[c_start] == '\t')) c_start++;
            int c_end = c_start;
            while (src[c_end] != '\0' && !is_delim(&src[c_end])) c_end++;
            if (c_end > c_start) register_type(&src[c_start], c_end - c_start);
            while (src[c_end] != '\0' && (src[c_end] == ' ' || src[c_end] == '\t')) c_end++;
            if (src[c_end] == ':') c_end++;
            i = c_end;
            continue;
        }

        if (src[i] == ':') {
            int end = i - 1;
            while (end >= 0 && isspace((unsigned char)src[end])) end--;
            int start = end;
            while (start >= 0 && !is_delim(&src[start])) start--;
            start++;
            if (end >= start) register_identifier(&src[start], end - start + 1);

            int t_start = i + 1;
            while (src[t_start] != '\0' && (src[t_start] == ' ' || src[t_start] == '\t')) t_start++;
            if (src[t_start] != '\n' && src[t_start] != '\r' && src[t_start] != '\0') {
                int t_end = t_start;
                while (src[t_end] != '\0' && !is_word_boundary(&src[t_end])) {
                    t_end += utf8_char_len((unsigned char)src[t_end]);
                }
                if (t_end > t_start) {
                    size_t t_len = t_end - t_start;
                    BuiltinKind kind;
                    if (!builtin_to_canonical(&src[t_start], t_len, LANG_JA, &kind)) {
                        register_type(&src[t_start], t_len);
                    }
                }
                i = t_end;
                continue;
            }
        }
        i++;
    }
}

static char* ja_preprocess(const char* src) {
    ja_reset();
    scan_declarations(src);

    size_t out_cap = strlen(src) * 3 + 1024;
    char* out = malloc(out_cap);
    if (!out) return NULL;

    int r = 0, w = 0;
    int str_stack[32];
    int str_stack_top = 0;

    int orig_line = 1, orig_col = 1;
    int clean_line = 1, clean_col = 1;

    while (src[r] != '\0') {
        bool in_string = (str_stack_top > 0 && str_stack[str_stack_top - 1] == 1);

        if (in_string) {
            if (src[r] == '\\' && src[r + 1] != '\0') {
                emit_char(out, &w, src[r++], &clean_line, &clean_col, orig_line, orig_col++);
                emit_char(out, &w, src[r++], &clean_line, &clean_col, orig_line, orig_col++);
                continue;
            }
            if (src[r] == '"') {
                emit_char(out, &w, src[r++], &clean_line, &clean_col, orig_line, orig_col++);
                str_stack_top--;
                continue;
            }
            if (src[r] == '{') {
                emit_char(out, &w, src[r++], &clean_line, &clean_col, orig_line, orig_col++);
                str_stack[str_stack_top++] = 0;
                continue;
            }
            int clen = utf8_char_len((unsigned char)src[r]);
            int width = get_char_width(&src[r]);
            int cur_c = orig_col;
            orig_col += width;
            while (clen-- > 0) emit_char(out, &w, src[r++], &clean_line, &clean_col, orig_line, cur_c);
            continue;
        }

        if (src[r] == '\n') {
            emit_char(out, &w, '\n', &clean_line, &clean_col, orig_line, orig_col);
            orig_line++;
            orig_col = 1;
            r++;
            continue;
        }

        if (src[r] == '/' && src[r + 1] == '/') {
            while (src[r] != '\n' && src[r] != '\0') {
                int clen = utf8_char_len((unsigned char)src[r]);
                int width = get_char_width(&src[r]);
                int cur_c = orig_col;
                orig_col += width;
                while (clen-- > 0) emit_char(out, &w, src[r++], &clean_line, &clean_col, orig_line, cur_c);
            }
            continue;
        }

        if (src[r] == '"') {
            emit_char(out, &w, src[r++], &clean_line, &clean_col, orig_line, orig_col++);
            str_stack[str_stack_top++] = 1;
            continue;
        }

        if (src[r] == '}' && str_stack_top > 0 && str_stack[str_stack_top - 1] == 0) {
            emit_char(out, &w, src[r++], &clean_line, &clean_col, orig_line, orig_col++);
            str_stack_top--;
            continue;
        }

        // 3. 日本語固有記号の置換
        if (strncmp(&src[r], "・", 3) == 0) { emit_char(out, &w, '.', &clean_line, &clean_col, orig_line, orig_col); orig_col += 2; r += 3; continue; }
        if (strncmp(&src[r], "、", 3) == 0) { emit_char(out, &w, ',', &clean_line, &clean_col, orig_line, orig_col); orig_col += 2; r += 3; continue; }
        if (strncmp(&src[r], "。", 3) == 0) { emit_char(out, &w, ';', &clean_line, &clean_col, orig_line, orig_col); orig_col += 2; r += 3; continue; }

        // 4. 先に保護！: 前後に水平スペースがある「助詞を含む識別子」
        if (r > 0 && is_blank_space(&src[r - 1]) && !is_after_indent(src, r) && src[r - 1] != ':' && (unsigned char)src[r] >= 0x80) {
            int end = r;
            bool has_inner_particle = false;
            int last_particle_end = -1;

            while (src[end] != '\0' && !is_blank_space(&src[end]) && !is_delim(&src[end])) {
                int p = get_particle_len(&src[end]);
                if (p > 0 && end > r) {
                    const char* after_p = &src[end + p];
                    if (*after_p != '\0' && !is_blank_space(after_p) && !is_delim(after_p)) {
                        has_inner_particle = true;
                    }
                    last_particle_end = end + p;
                }
                end += utf8_char_len((unsigned char)src[end]);
            }

            bool ends_with_particle = (last_particle_end == end);

            // 直後が水平スペースであること
            if (has_inner_particle && !ends_with_particle && is_blank_space(&src[end])) {
                int len = end - r;
                int start_col = orig_col;
                for (int k = 0; k < len; ) {
                    int clen = utf8_char_len((unsigned char)src[r + k]);
                    orig_col += get_char_width(&src[r + k]);
                    k += clen;
                }
                int end_col = orig_col - 1;
                const char* ph = register_identifier(&src[r], len);
                r = end;
                emit_token_str(out, &w, ph, &clean_line, &clean_col, orig_line, start_col, end_col);
                continue;
            }
        }

        // 5. 予約語置換 (class, is, return, 空, etc.)
        size_t kw_len = 0;
        const KeywordDef* kw = keyword_match_prefix(&src[r], LANG_JA, &kw_len);
        if (kw) {
            bool is_ha = (strcmp(kw->replacement, " is ") == 0);
            bool has_trailing_space = (kw->replacement[strlen(kw->replacement) - 1] == ' ');
            const char* next_p = &src[r + kw_len];

            if (is_ha || has_trailing_space || is_word_boundary(next_p)) {
                int start_col = orig_col;
                for (size_t k = 0; k < kw_len; ) {
                    int clen = utf8_char_len((unsigned char)src[r + k]);
                    orig_col += get_char_width(&src[r + k]);
                    k += clen;
                }
                int end_col = orig_col - 1;
                r += kw_len;
                emit_token_str(out, &w, kw->replacement, &clean_line, &clean_col, orig_line, start_col, end_col);
                continue;
            }
        }

        // 6. 組み込み型・組み込みメソッド
        size_t b_len = 0;
        const BuiltinDef* bdef = builtin_match_prefix(&src[r], LANG_JA, &b_len);
        if (bdef) {
            const char* next_p = &src[r + b_len];
            if (is_word_boundary(next_p)) {
                int start_col = orig_col;
                for (size_t k = 0; k < b_len; ) {
                    int clen = utf8_char_len((unsigned char)src[r + k]);
                    orig_col += get_char_width(&src[r + k]);
                    k += clen;
                }
                int end_col = orig_col - 1;
                r += b_len;
                emit_token_str(out, &w, bdef->canonical_en, &clean_line, &clean_col, orig_line, start_col, end_col);
                continue;
            }
        }

        // 7. 登録済みシンボル置換
        int best_match = -1, best_len = 0;
        for (int m = 0; m < ident_count; m++) {
            int len = strlen(ident_map[m].original);
            if (strncmp(&src[r], ident_map[m].original, len) == 0 && len > best_len) {
                best_len = len;
                best_match = m;
            }
        }
        if (best_match != -1) {
            const char* next_p = &src[r + best_len];
            if (is_word_boundary(next_p)) {
                int start_col = orig_col;
                for (int k = 0; k < best_len; ) {
                    int clen = utf8_char_len((unsigned char)src[r + k]);
                    orig_col += get_char_width(&src[r + k]);
                    k += clen;
                }
                int end_col = orig_col - 1;
                const char* ph = ident_map[best_match].placeholder;
                r += best_len;
                emit_token_str(out, &w, ph, &clean_line, &clean_col, orig_line, start_col, end_col);
                continue;
            }
        }

        // 8. 助詞のスキップ
        int p_len = get_particle_len(&src[r]);
        if (p_len > 0) {
            int start_col = orig_col;
            for (int k = 0; k < p_len; ) {
                int clen = utf8_char_len((unsigned char)src[r + k]);
                orig_col += get_char_width(&src[r + k]);
                k += clen;
            }
            r += p_len;
            emit_char(out, &w, ' ', &clean_line, &clean_col, orig_line, start_col);
            continue;
        }

        // 9. 未知の日本語識別子の自動登録
        if ((unsigned char)src[r] >= 0x80) {
            int id_start = r;
            int start_col = orig_col;
            while (src[r] != '\0' && !is_word_boundary(&src[r])) {
                orig_col += get_char_width(&src[r]);
                r += utf8_char_len((unsigned char)src[r]);
            }
            int id_len = r - id_start;
            int end_col = orig_col - 1;
            if (id_len > 0) {
                const char* ph = register_identifier(&src[id_start], id_len);
                emit_token_str(out, &w, ph, &clean_line, &clean_col, orig_line, start_col, end_col);
                continue;
            }
        }

        // 10. その他 (ASCII 英数字・記号・空白)
        int clen = utf8_char_len((unsigned char)src[r]);
        int width = get_char_width(&src[r]);
        int cur_c = orig_col;
        orig_col += width;
        while (clen-- > 0) emit_char(out, &w, src[r++], &clean_line, &clean_col, orig_line, cur_c);
    }

    out[w] = '\0';
    return out;
}

// 日本語フロントエンド実体
LanguageFrontend ja_frontend = {
    .lang = LANG_JA,
    .ext_script = "jje",
    .ext_class  = "jcd",
    .preprocess = ja_preprocess,
    .map_loc    = ja_map_loc,
    .reset      = ja_reset,
    .lookup_symbol = ja_lookup_symbol,
    .register_module_dict = NULL
};
