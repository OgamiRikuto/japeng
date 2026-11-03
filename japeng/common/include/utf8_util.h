#ifndef UTF8_UTIL_H
#define UTF8_UTIL_H

#include <stdint.h>

// 1バイト目からUTF-8文字のバイト長 (1〜4) を判定
static inline int utf8_char_len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

// バイト長から文字数（グリフ数）をカウント
static inline uint32_t utf8_char_count(const char* s, uint32_t byte_len) {
    uint32_t count = 0;
    uint32_t i = 0;
    while (i < byte_len) {
        i += utf8_char_len((unsigned char)s[i]);
        count++;
    }
    return count;
}

#endif
