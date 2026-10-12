#ifndef PREPROCESSOR_H_
#define PREPROCESSOR_H_

#include <stdbool.h>

// 日本語ソースコードを前処理し、Flex/Bisonが直接解釈できるASCIIコード文字列を生成（呼び出し側で free 必要）
char* preprocess(const char* src);

// プレースホルダー（__id_X__）から元の日本語名を逆引き
const char* lookup_original_symbol(const char* placeholder);

// ファイルパースごとにシンボルマップをリセット
void reset_preprocessor(void);

#endif
