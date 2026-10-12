#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

// --- パス 1: 全角の半角正規化（文字列・コメント保護） ---
static char* normalize_chars(const char* src) 
{
    size_t len = strlen(src);
    char* out = malloc(len * 2 + 1);
    if (!out) return NULL;

    int r = 0, w = 0;
    while (src[r] != '\0') {
        // 1. コメント保護
        if (src[r] == '/' && src[r + 1] == '/') {
            while (src[r] != '\n' && src[r] != '\0') out[w++] = src[r++];
            continue;
        }

        // 2. 文字列保護 ("...")
        if (src[r] == '"') {
            out[w++] = src[r++];
            while (src[r] != '"' && src[r] != '\0') {
                if (src[r] == '\\' && src[r + 1] != '\0') out[w++] = src[r++];
                out[w++] = src[r++];
            }
            if (src[r] == '"') out[w++] = src[r++];
            continue;
        }

        // ★ 2.5 全角鉤括弧 「...」 ➜ 半角 "..." に正規化（中身の文字列は保護）
        if (strncmp(&src[r], "「", 3) == 0) {
            out[w++] = '"';
            r += 3;
            while (src[r] != '\0' && strncmp(&src[r], "」", 3) != 0) {
                if (src[r] == '\\' && src[r + 1] != '\0') out[w++] = src[r++];
                out[w++] = src[r++];
            }
            if (strncmp(&src[r], "」", 3) == 0) {
                out[w++] = '"';
                r += 3;
            }
            continue;
        }

        // 3. 全角空白 (U+3000) ➜ 半角スペース
        if ((unsigned char)src[r] == 0xE3 && (unsigned char)src[r + 1] == 0x80 && (unsigned char)src[r + 2] == 0x80) {
            out[w++] = ' ';
            r += 3;
            continue;
        }

        // 4. 全角長音・全角マイナス ➜ '-'
        if (((unsigned char)src[r] == 0xE3 && (unsigned char)src[r + 1] == 0x83 && (unsigned char)src[r + 2] == 0xBC) ||
            ((unsigned char)src[r] == 0xE2 && (unsigned char)src[r + 1] == 0x88 && (unsigned char)src[r + 2] == 0x92)) {
            out[w++] = '-';
            r += 3;
            continue;
        }

        // 5. ★【修正】全角ASCII領域 (U+FF01〜U+FF5E) の半角化
        if ((unsigned char)src[r] == 0xEF) {
            unsigned char b2 = (unsigned char)src[r + 1];
            unsigned char b3 = (unsigned char)src[r + 2];

            // 0xEF 0xBC 0x81 ('！') 〜 0xEF 0xBC 0xBF ('＿')
            // ★ (b3 - 0x81 + 0x21) ＝ (b3 - 0x60) で 0x81 がぴったり '!' (0x21) になる！
            if (b2 == 0xBC && b3 >= 0x81 && b3 <= 0xBF) {
                out[w++] = (char)(b3 - 0x60);
                r += 3;
                continue;
            }

            // 0xEF 0xBD 0x80 ('｀') 〜 0xEF 0xBD 0x9E ('～')
            // 0x80 がぴったり '`' (0x60) になる
            if (b2 == 0xBD && b3 >= 0x80 && b3 <= 0x9E) {
                out[w++] = (char)(b3 - 0x20);
                r += 3;
                continue;
            }
        }

        out[w++] = src[r++];
    }
    out[w] = '\0';
    return out;
}

// --- パス 2: 行インデント整形 ---
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

        // ★ 新しいクラス宣言が来たらインデントをリセット
        if (strncmp(&normalized[content_start], "クラス", 9) == 0) {
            indent_level = 0;
        }

        // 行頭が閉じ括弧「』」で始まる場合、この行自体のインデントを下げる
        if (strncmp(&normalized[content_start], "』", 3) == 0) {
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
            if (strncmp(&normalized[i], "『", 3) == 0) {
                indent_level++;
                break;
            }
        }
    }

    out[w] = '\0';
    free(normalized);
    return out;
}
