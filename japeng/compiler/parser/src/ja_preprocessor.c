#include "preprocessor.h"
#include "builtin_convert.h"
#include "utf8_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#define MAX_IDENTS 2048

extern LanguageFrontend ja_frontend;

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

static inline bool is_delim(const char* p) {
    if (*p == '\0') return true;
    unsigned char c = (unsigned char)*p;
    if (isspace(c) || c == '(' || c == ')' || c == '[' || c == ']' ||
        c == '{' || c == '}' || c == '<' || c == '>' || c == '=' ||
        c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
        c == '!' || c == '&' || c == '|' || c == '^' || c == '~' ||
        c == ',' || c == '.' || c == ';' || c == ':' || c == '"') {
        return true;
    }
    // 全角スペース(3000)・読点(3001)・句点(3002)
    if (p[0] == '\xE3' && p[1] == '\x80' && (c == 0xE3 && ((unsigned char)p[2] == 0x80 || (unsigned char)p[2] == 0x81 || (unsigned char)p[2] == 0x82))) {
        return true;
    }
    // 中黒(30FB)
    if ((unsigned char)p[0] == 0xE3 && (unsigned char)p[1] == 0x83 && (unsigned char)p[2] == 0xBB) return true;
    // 鉤括弧・二重鉤括弧(300C〜300F)
    if ((unsigned char)p[0] == 0xE3 && (unsigned char)p[1] == 0x80 &&
        ((unsigned char)p[2] >= 0x8C && (unsigned char)p[2] <= 0x8F)) return true;
    return false;
}

// 単語境界判定（区切り記号、またはドット化・空白化される予約語/助詞が直後に来るか）
static inline bool is_word_boundary(const char* p) {
    if (is_delim(p)) return true;
    size_t kw_len = 0;
    const DictEntry* kw = keyword_match_prefix(p, ja_frontend.lang_code, &kw_len);
    if (kw != NULL) {
        // 助詞・接続語・代入「は」・回数指定は単語の途中で境界になる
        if (kw->canonical_en[0] == '\0' || kw->canonical_en[0] == '.' || strcmp(kw->canonical_en, "=") == 0) {
            return true;
        }
    }
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

// 末尾が「型」（UTF-8: E5 9E 8B）で4バイト以上なら CLASSTYPE_X、それ以外は __id_X__
static const char* register_ja_symbol(const char* name, int len) {
    if (len <= 0 || len >= 256) return NULL;
    char temp[256];
    memcpy(temp, name, len);
    temp[len] = '\0';

    for (int i = 0; i < ident_count; i++) {
        if (strcmp(ident_map[i].original, temp) == 0) return ident_map[i].placeholder;
    }
    if (ident_count >= MAX_IDENTS) exit(EXIT_FAILURE);

    bool is_type = (len > 3 &&
                    (unsigned char)temp[len - 3] == 0xE5 &&
                    (unsigned char)temp[len - 2] == 0x9E &&
                    (unsigned char)temp[len - 1] == 0x8B);

    strncpy(ident_map[ident_count].original, temp, sizeof(ident_map[ident_count].original) - 1);
    if (is_type) {
        snprintf(ident_map[ident_count].placeholder, sizeof(ident_map[ident_count].placeholder), "CLASSTYPE_%d", type_count++);
    } else {
        snprintf(ident_map[ident_count].placeholder, sizeof(ident_map[ident_count].placeholder), "__id_%d__", ident_count);
    }
    ident_count++;
    return ident_map[ident_count - 1].placeholder;
}

static char* ja_preprocess(const char* src) {
    ja_reset();

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

        // 1. 日本語固有記号の置換
        if (strncmp(&src[r], "・", 3) == 0) { emit_char(out, &w, '.', &clean_line, &clean_col, orig_line, orig_col); orig_col += 2; r += 3; continue; }
        if (strncmp(&src[r], "、", 3) == 0) { emit_char(out, &w, ',', &clean_line, &clean_col, orig_line, orig_col); orig_col += 2; r += 3; continue; }
        if (strncmp(&src[r], "。", 3) == 0) { emit_char(out, &w, ';', &clean_line, &clean_col, orig_line, orig_col); orig_col += 2; r += 3; continue; }

        // 2. 予約語・助詞・接続語・回数指定の置換 (lib/lang/reserved.dict.ja)
        size_t kw_len = 0;
        const DictEntry* kw = keyword_match_prefix(&src[r], ja_frontend.lang_code, &kw_len);
        if (kw) {
            const char* next_p = &src[r + kw_len];
            bool is_particle_or_op = (kw->canonical_en[0] == '\0' ||
                                      kw->canonical_en[0] == '.' ||
                                      strcmp(kw->canonical_en, "=") == 0);

            if (is_particle_or_op || is_word_boundary(next_p)) {
                int start_col = orig_col;
                for (size_t k = 0; k < kw_len; ) {
                    int clen = utf8_char_len((unsigned char)src[r + k]);
                    orig_col += get_char_width(&src[r + k]);
                    k += clen;
                }
                int end_col = orig_col - 1;
                r += kw_len;

                if (kw->canonical_en[0] == '\0') {
                    // 右辺が空（なら、の間 等）は空白 1 文字へ置換
                    emit_char(out, &w, ' ', &clean_line, &clean_col, orig_line, start_col);
                } else {
                    emit_token_str(out, &w, kw->canonical_en, &clean_line, &clean_col, orig_line, start_col, end_col);
                    // 英語キーワードの直後に識別子やリテラルが密着している場合は 1 スペース補完
                    if (kw->canonical_en[0] != '.' && strcmp(kw->canonical_en, "=") != 0 && !is_delim(next_p)) {
                        emit_char(out, &w, ' ', &clean_line, &clean_col, orig_line, end_col);
                    }
                }
                continue;
            }
        }

        // 3. 組み込み型・組み込みメソッドの置換 (lib/builtin/dict/*.dict.ja)
        size_t b_len = 0;
        const DictEntry* bdef = builtin_match_prefix(&src[r], ja_frontend.lang_code, &b_len);
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

        // 4. 日本語識別子 / クラス型名の切り出しとプレースホルダー登録
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
                const char* ph = register_ja_symbol(&src[id_start], id_len);
                emit_token_str(out, &w, ph, &clean_line, &clean_col, orig_line, start_col, end_col);
                continue;
            }
        }

        // 5. その他 (ASCII 英数字・記号・空白)
        int clen = utf8_char_len((unsigned char)src[r]);
        int width = get_char_width(&src[r]);
        int cur_c = orig_col;
        orig_col += width;
        while (clen-- > 0) emit_char(out, &w, src[r++], &clean_line, &clean_col, orig_line, cur_c);
    }

    out[w] = '\0';
    return out;
}

LanguageFrontend ja_frontend = {
    .lang = LANG_JA,
    .lang_code = "ja",
    .ext_script = "jje",
    .ext_class  = "jcd",
    .preprocess = ja_preprocess,
    .map_loc    = ja_map_loc,
    .reset      = ja_reset,
    .lookup_symbol = ja_lookup_symbol,
    .register_module_dict = NULL
};
