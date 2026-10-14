#include "preprocessor.h"
#include "builtin_convert.h"
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

void reset_preprocessor(void) {
    ident_count = 0;
    type_count = 0;
}

static inline int utf8_char_len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static inline bool is_blank_space(const char* p) {
    if (*p == ' ' || *p == '\t') return true;
    if ((unsigned char)p[0] == 0xE3 && (unsigned char)p[1] == 0x80 && (unsigned char)p[2] == 0x80) return true;
    return false;
}

// 行頭のインデント直後かどうかを判定するヘルパー
static inline bool is_after_indent(const char* src, int r) {
    int p = r - 1;
    while (p >= 0 && (src[p] == ' ' || src[p] == '\t' ||
           ((unsigned char)src[p] == 0x80 && p >= 2 && (unsigned char)src[p-2] == 0xE3))) {
        if ((unsigned char)src[p] == 0x80) p -= 3;
        else p--;
    }
    // 行頭（ファイルの先頭、または改行の直後）ならインデント
    if (p < 0 || src[p] == '\n' || src[p] == '\r') {
        return true;
    }
    return false;
}

static inline bool is_delim(const char* p) {
    if (*p == '\0') return true;
    char c = *p;
    if (isspace((unsigned char)c) || c == '(' || c == ')' || c == '[' || c == ']' ||
        c == '{' || c == '}' || c == ',' || c == '.' || c == ';' || c == ':' || c == '"') {
        return true;
    }
    // 句点「。」、読点「、」、中黒「・」、全角空白
    if ((unsigned char)p[0] == 0xE3 && (unsigned char)p[1] == 0x80 && 
        ((unsigned char)p[2] == 0x80 || (unsigned char)p[2] == 0x81 || (unsigned char)p[2] == 0x82)) return true;
    if ((unsigned char)p[0] == 0xE3 && (unsigned char)p[1] == 0x83 && (unsigned char)p[2] == 0xBB) return true;
    // 鉤括弧「」『』
    if ((unsigned char)p[0] == 0xE3 && (unsigned char)p[1] == 0x80 && 
        ((unsigned char)p[2] >= 0x8C && (unsigned char)p[2] <= 0x8F)) return true;

    return false;
}

static inline int get_particle_len(const char* p) {
    if (strncmp(p, "から", 6) == 0 || strncmp(p, "より", 6) == 0 ||
        strncmp(p, "なら", 6) == 0 || strncmp(p, "の間", 6) == 0) return 6;
    if (strncmp(p, "が", 3) == 0 || strncmp(p, "に", 3) == 0 ||
        strncmp(p, "へ", 3) == 0 || strncmp(p, "で", 3) == 0 ||
        strncmp(p, "を", 3) == 0 || strncmp(p, "の", 3) == 0) return 3;
    return 0;
}

// ★【超重要】単語の境界（区切り記号、助詞、または「は」）
static inline bool is_word_boundary(const char* p) {
    if (is_delim(p)) return true;
    if (get_particle_len(p) > 0) return true;
    if (strncmp(p, "は", 3) == 0) return true; // 「は」も単語の切れ目！
    return false;
}

const char* lookup_original_symbol(const char* placeholder) {
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
        if (strcmp(ident_map[i].original, temp) == 0) {
            return ident_map[i].placeholder;
        }
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
        if (strcmp(ident_map[i].original, temp) == 0) {
            return ident_map[i].placeholder;
        }
    }
    if (ident_count >= MAX_IDENTS) exit(EXIT_FAILURE);

    strncpy(ident_map[ident_count].original, temp, sizeof(ident_map[ident_count].original) - 1);
    snprintf(ident_map[ident_count].placeholder, sizeof(ident_map[ident_count].placeholder), "CLASSTYPE_%d", type_count++);
    ident_count++;
    return ident_map[ident_count - 1].placeholder;
}

// --- パス 1: 宣言の走査 ---
static void scan_declarations(const char* src) {
    int i = 0;
    while (src[i] != '\0') {
        if (src[i] == '/' && src[i + 1] == '/') {
            while (src[i] != '\n' && src[i] != '\0') i++;
            continue;
        }
        if (src[i] == '"') {
            i++;
            while (src[i] != '"' && src[i] != '\0') {
                if (src[i] == '\\' && src[i + 1] != '\0') i++;
                i++;
            }
            if (src[i] == '"') i++;
            continue;
        }

        // 1. クラス宣言（クラス クラス名:）
        size_t kw_len = 0;
        const KeywordDef* kw = keyword_match_prefix(&src[i], compile_lang, &kw_len);
        if (kw && strcmp(kw->replacement, "class ") == 0) {
            int c_start = i + kw_len;
            while (src[c_start] != '\0' && (src[c_start] == ' ' || src[c_start] == '\t')) c_start++;
            int c_end = c_start;
            while (src[c_end] != '\0' && !is_delim(&src[c_end])) c_end++;
            if (c_end > c_start) {
                register_type(&src[c_start], c_end - c_start);
            }
            while (src[c_end] != '\0' && (src[c_end] == ' ' || src[c_end] == '\t')) c_end++;
            if (src[c_end] == ':') c_end++;
            i = c_end;
            continue;
        }

        // 2. 変数・フィールド宣言
        if (src[i] == ':') {
            int end = i - 1;
            while (end >= 0 && isspace((unsigned char)src[end])) end--;
            int start = end;
            while (start >= 0 && !is_delim(&src[start])) start--;
            start++;
            if (end >= start) {
                register_identifier(&src[start], end - start + 1);
            }

            int t_start = i + 1;
            while (src[t_start] != '\0' && (src[t_start] == ' ' || src[t_start] == '\t')) t_start++;
            if (src[t_start] != '\n' && src[t_start] != '\r' && src[t_start] != '\0') {
                int t_end = t_start;
                // 「は」や区切り文字の手前までを型名とする
                while (src[t_end] != '\0' && !is_word_boundary(&src[t_end])) {
                    t_end += utf8_char_len((unsigned char)src[t_end]);
                }
                if (t_end > t_start) {
                    size_t t_len = t_end - t_start;
                    BuiltinKind kind;
                    if (!builtin_to_canonical(&src[t_start], t_len, current_lang, &kind)) {
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

// --- パス 2: 本番変換 ---
char* preprocess(const char* src) {
    reset_preprocessor();
    scan_declarations(src);

    size_t out_cap = strlen(src) * 3 + 1024;
    char* out = malloc(out_cap);
    if (!out) return NULL;

    int r = 0, w = 0;

    while (src[r] != '\0') {
        // 1. コメント
        if (src[r] == '/' && src[r + 1] == '/') {
            while (src[r] != '\n' && src[r] != '\0') out[w++] = src[r++];
            continue;
        }

        // 2. 文字列リテラル
        if (src[r] == '"') {
            out[w++] = src[r++];
            while (src[r] != '"' && src[r] != '\0') {
                if (src[r] == '\\' && src[r + 1] != '\0') out[w++] = src[r++];
                out[w++] = src[r++];
            }
            if (src[r] == '"') out[w++] = src[r++];
            continue;
        }

        // 3. 日本語固有記号の置換
        if (strncmp(&src[r], "・", 3) == 0) { out[w++] = '.'; r += 3; continue; }
        if (strncmp(&src[r], "、", 3) == 0) { out[w++] = ','; r += 3; continue; }
        if (strncmp(&src[r], "。", 3) == 0) { out[w++] = ';'; r += 3; continue; }
        if (strncmp(&src[r], "「", 3) == 0 || strncmp(&src[r], "」", 3) == 0) { out[w++] = '"'; r += 3; continue; }
        if (strncmp(&src[r], "『", 3) == 0) { out[w++] = '['; r += 3; continue; }
        if (strncmp(&src[r], "』", 3) == 0) { out[w++] = ']'; r += 3; continue; }

        // 4. 予約語置換
        size_t kw_len = 0;
        const KeywordDef* kw = keyword_match_prefix(&src[r], current_lang, &kw_len);
        if (kw) {
            bool is_ha = (strcmp(kw->replacement, " is ") == 0);
            // 置換先が末尾スペース付き（"return ", "class " 等）なら直後が単語でも安全に置換できる
            bool has_trailing_space = (kw->replacement[strlen(kw->replacement) - 1] == ' ');
            const char* next_p = &src[r + kw_len];

            if (is_ha || has_trailing_space || is_word_boundary(next_p)) {
                size_t rep_len = strlen(kw->replacement);
                strcpy(&out[w], kw->replacement);
                w += rep_len;
                r += kw_len;
                continue;
            }
        }

        // 5. ★【改修】前後に水平スペースがある「助詞を含む識別子」（例: " 名前の設定 "）
        // ※ 行頭のインデント直後ではなく、かつ直前がコロンでもない「文の途中」の空白のみ対象！
        if (r > 0 && is_blank_space(&src[r - 1]) && !is_after_indent(src, r) && src[r - 1] != ':' && (unsigned char)src[r] >= 0x80) {
            int end = r;
            bool has_inner_particle = false;
            int last_particle_end = -1;

            while (src[end] != '\0' && !is_blank_space(&src[end]) && !is_delim(&src[end])) {
                int p = get_particle_len(&src[end]);
                if (p > 0 && end > r) {
                    const char* after_p = &src[end + p];
                    // ★ 助詞の直後が空白や区切りでなければ「中間助詞」
                    if (*after_p != '\0' && !is_blank_space(after_p) && !is_delim(after_p)) {
                        has_inner_particle = true;
                    }
                    last_particle_end = end + p;
                }
                end += utf8_char_len((unsigned char)src[end]);
            }

            // ★ 末尾が助詞で終わっている単語（例: "タロウの"）は識別子ではないので除外！
            bool ends_with_particle = (last_particle_end == end);

            if (has_inner_particle && !ends_with_particle && is_blank_space(&src[end])) {
                int len = end - r;
                const char* ph = register_identifier(&src[r], len);
                strcpy(&out[w], ph);
                w += strlen(ph);
                r = end;
                continue;
            }
        }

        // 5.5 組み込み型・組み込みメソッド（直後が境界なら置換）
        size_t b_len = 0;
        const BuiltinDef* bdef = builtin_match_prefix(&src[r], current_lang, &b_len);
        if (bdef) {
            const char* next_p = &src[r + b_len];
            if (is_word_boundary(next_p)) {
                size_t en_len = strlen(bdef->canonical_en);
                strcpy(&out[w], bdef->canonical_en);
                w += en_len;
                r += b_len;
                continue;
            }
        }

        // 6. 登録済みシンボル置換
        int best_match = -1, best_len = 0;
        for (int m = 0; m < ident_count; m++) {
            int len = strlen(ident_map[m].original);
            if (strncmp(&src[r], ident_map[m].original, len) == 0) {
                if (len > best_len) {
                    best_len = len;
                    best_match = m;
                }
            }
        }
        if (best_match != -1) {
            const char* next_p = &src[r + best_len];
            if (is_word_boundary(next_p)) {
                const char* ph = ident_map[best_match].placeholder;
                strcpy(&out[w], ph);
                w += strlen(ph);
                r += best_len;
                continue;
            }
        }

        // 7. 助詞のスキップ（空白化）
        int p_len = get_particle_len(&src[r]);
        if (p_len > 0) {
            out[w++] = ' ';
            r += p_len;
            continue;
        }

        // 8. 未知の日本語識別子の自動登録（境界まで読み進める）
        if ((unsigned char)src[r] >= 0x80) {
            int id_start = r;
            while (src[r] != '\0' && !is_word_boundary(&src[r])) {
                r += utf8_char_len((unsigned char)src[r]);
            }
            int id_len = r - id_start;
            if (id_len > 0) {
                const char* ph = register_identifier(&src[id_start], id_len);
                strcpy(&out[w], ph);
                w += strlen(ph);
            }
            continue;
        }

        // 9. その他
        out[w++] = src[r++];
    }

    out[w] = '\0';
    return out;
}
