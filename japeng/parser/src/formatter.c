#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

// =============================================================================
// 1. 日本語記号 ➜ ASCII 変換テーブル
// =============================================================================
typedef struct {
    const char* ja;     // 日本語・全角記号 (UTF-8)
    const char* ascii;  // 置換先 ASCII 文字列
} SymbolMapping;

static const SymbolMapping symbol_table[] = {
    // 空白・区切り
    { " ", " " },    // 全角スペース (U+3000)
    { "・", "." },    // 中黒
    { "、", "," },    // 読点
    { "。", ";" },    // 句点
    { "：", ":" },    // 全角コロン
    { "；", ";" },    // 全角セミコロン

    // 括弧類
    { "「", "\"" },   // 鉤括弧 ➜ 文字列
    { "」", "\"" },
    { "『", "[" },    // 二重鉤括弧 ➜ ブロック
    { "』", "]" },
    { "（", "(" },    // 全角丸括弧
    { "）", ")" },
    { "［", "[" },    // 全角角括弧
    { "］", "]" },
    { "｛", "{" },    // ★ 全角波括弧 ➜ 補間式
    { "｝", "}" },

    // 演算子・記号
    { "＝", "=" },
    { "＋", "+" },
    { "＊", "*" },
    { "／", "/" },
    { "％", "%" },
    { "＜", "<" },
    { "＞", ">" },
    { "！", "!" },
    { "＆", "&" },
    { "｜", "|" },
    { "＾", "^" },
    { "〜", "~" },
    { "−", "-" },    // 全角マイナス (U+2212)

    { NULL, NULL }    // 番兵
};

// =============================================================================
// 2. 判定ヘルパー
// =============================================================================

// 直前がカタカナか判定（長音「ー」が伸ばし棒かマイナスかを区別するため）
static inline bool is_prev_katakana(const char* src, int r) {
    if (r < 3) return false;
    unsigned char b1 = (unsigned char)src[r - 3];
    unsigned char b2 = (unsigned char)src[r - 2];
    unsigned char b3 = (unsigned char)src[r - 1];

    if (b1 == 0xE3) {
        if (b2 == 0x82 && b3 >= 0xA1) return true; // ァ 〜 タ
        if (b2 == 0x83 && b3 <= 0xBF) return true; // ダ 〜 ヿ
    }
    return false;
}

// テーブルから一致する記号を検索
static const SymbolMapping* find_symbol_mapping(const char* p, size_t* out_len) {
    for (int i = 0; symbol_table[i].ja != NULL; i++) {
        size_t len = strlen(symbol_table[i].ja);
        if (strncmp(p, symbol_table[i].ja, len) == 0) {
            if (out_len) *out_len = len;
            return &symbol_table[i];
        }
    }
    return NULL;
}

// =============================================================================
// 3. パス 1: 全角 ➜ 半角正規化 (テーブル駆動 ＋ 文字列補間対応)
// =============================================================================
static char* normalize_chars(const char* src) 
{
    size_t len = strlen(src);
    char* out = malloc(len * 2 + 1);
    if (!out) return NULL;

    int r = 0, w = 0;
    bool in_string = false;
    int interp_brace_depth = 0; // 文字列内の { } ネスト深さ

    while (src[r] != '\0') {
        // --- A. コメント保護 (行末までスキップ) ---
        if (!in_string && src[r] == '/' && src[r + 1] == '/') {
            while (src[r] != '\n' && src[r] != '\0') out[w++] = src[r++];
            continue;
        }

        // --- B. 文字列の開始・終了判定 (" または 「」) ---
        if (interp_brace_depth == 0) {
            // ダブルクォート "
            if (src[r] == '"') {
                in_string = !in_string;
                out[w++] = src[r++];
                continue;
            }
            // 鉤括弧 「 または 」 ➜ 半角 " に変換しつつ文字列モード切替
            if (strncmp(&src[r], "「", 3) == 0 || strncmp(&src[r], "」", 3) == 0) {
                in_string = !in_string;
                out[w++] = '"';
                r += 3;
                continue;
            }
        }

        // --- C. 文字列内の処理 ---
        if (in_string) {
            // エスケープシーケンス (\") や (\{) はそのまま通過
            if (src[r] == '\\' && src[r + 1] != '\0') {
                out[w++] = src[r++];
                out[w++] = src[r++];
                continue;
            }

            // ★ 波括弧 { または ｛ ➜ 補間式モード突入
            if (src[r] == '{' || strncmp(&src[r], "｛", 3) == 0) {
                interp_brace_depth++;
                out[w++] = '{';
                r += (src[r] == '{') ? 1 : 3;
                continue;
            }

            // ★ 閉じ波括弧 } または ｝ ➜ 文字列モード復帰
            if (interp_brace_depth > 0 && (src[r] == '}' || strncmp(&src[r], "｝", 3) == 0)) {
                interp_brace_depth--;
                out[w++] = '}';
                r += (src[r] == '}') ? 1 : 3;
                continue;
            }

            // 補間式の中（interp_brace_depth > 0）でない「純粋な文字列の中」なら無変換で通過
            if (interp_brace_depth == 0) {
                out[w++] = src[r++];
                continue;
            }
            // ※ 補間式の中（interp_brace_depth > 0）なら、下の正規化処理へ流す！
        }

        // --- D. テーブル駆動による記号置換 ---
        size_t match_len = 0;
        const SymbolMapping* mapping = find_symbol_mapping(&src[r], &match_len);
        if (mapping != NULL) {
            size_t rep_len = strlen(mapping->ascii);
            memcpy(&out[w], mapping->ascii, rep_len);
            w += rep_len;
            r += match_len;
            continue;
        }

        // --- E. 特殊規則: カタカナ長音「ー」の判定 ---
        if ((unsigned char)src[r] == 0xE3 && (unsigned char)src[r + 1] == 0x83 && (unsigned char)src[r + 2] == 0xBC) {
            if (is_prev_katakana(src, r)) {
                // カタカナの直後なら「伸ばし棒」として維持
                out[w++] = src[r++];
                out[w++] = src[r++];
                out[w++] = src[r++];
            } else {
                // カタカナ以外（数字や空白の直後）ならマイナス演算子 '-'
                out[w++] = '-';
                r += 3;
            }
            continue;
        }

        // --- F. 全角英数 (U+FF01〜U+FF5E) の一括半角化 ---
        if ((unsigned char)src[r] == 0xEF) {
            unsigned char b2 = (unsigned char)src[r + 1];
            unsigned char b3 = (unsigned char)src[r + 2];

            // 0xEF 0xBC 0x81 ('！') 〜 0xEF 0xBC 0xBF ('＿')
            if (b2 == 0xBC && b3 >= 0x81 && b3 <= 0xBF) {
                out[w++] = (char)(b3 - 0x60);
                r += 3;
                continue;
            }
            // 0xEF 0xBD 0x80 ('｀') 〜 0xEF 0xBD 0x9E ('～')
            if (b2 == 0xBD && b3 >= 0x80 && b3 <= 0x9E) {
                out[w++] = (char)(b3 - 0x20);
                r += 3;
                continue;
            }
        }

        // その他の文字（日本語識別子など）はそのままコピー
        out[w++] = src[r++];
    }

    out[w] = '\0';
    return out;
}

// =============================================================================
// 4. パス 2: 行インデント整形 (既存ロジックを維持)
// =============================================================================
char* format_code(const char* src) 
{
    char* normalized = normalize_chars(src);
    if (!normalized) return NULL;

    size_t out_cap = strlen(normalized) * 2 + 1024;
    char* out = malloc(out_cap);
    if (!out) { free(normalized); return NULL; }

    int indent_level = 0;
    int r = 0, w = 0;
    int consecutive_empty_lines = 0;

    while (normalized[r] != '\0') {
        int line_start = r;
        while (normalized[r] != '\n' && normalized[r] != '\0') r++;
        int line_end = r;
        if (normalized[r] == '\n') r++;

        int content_start = line_start;
        while (content_start < line_end && (normalized[content_start] == ' ' || normalized[content_start] == '\t')) {
            content_start++;
        }

        int content_end = line_end;
        while (content_end > content_start && (normalized[content_end - 1] == ' ' || normalized[content_end - 1] == '\t')) {
            content_end--;
        }

        if (content_start == content_end) {
            if (consecutive_empty_lines < 1 && w > 0) {
                out[w++] = '\n';
                consecutive_empty_lines++;
            }
            continue;
        }
        consecutive_empty_lines = 0;

        // クラス宣言でインデントリセット
        if (strncmp(&normalized[content_start], "class", 5) == 0 ||
            strncmp(&normalized[content_start], "クラス", 9) == 0) {
            indent_level = 0;
        }

        // 閉じ括弧 ] でインデントを下げる
        if (normalized[content_start] == ']' || strncmp(&normalized[content_start], "』", 3) == 0) {
            if (indent_level > 0) indent_level--;
        }

        for (int i = 0; i < indent_level * 4; i++) {
            out[w++] = ' ';
        }

        for (int i = content_start; i < content_end; i++) {
            out[w++] = normalized[i];
        }
        out[w++] = '\n';

        if (normalized[content_end - 1] == ':') {
            indent_level++;
        }

        for (int i = content_end - 1; i >= content_start && i >= content_end - 6; i--) {
            if (normalized[i] == '[' || strncmp(&normalized[i], "『", 3) == 0) {
                indent_level++;
                break;
            }
        }
    }

    out[w] = '\0';
    free(normalized);
    return out;
}
