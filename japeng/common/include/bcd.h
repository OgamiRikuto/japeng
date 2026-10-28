#ifndef BCD_H
#define BCD_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define BCD_MAGIC "JPEN"
#define BCD_VERSION 1
#define BCD_MAX_SOURCE_NAME 64

typedef enum {
    BCD_TYPE_CLASS  = 1, // .bcd (クラス定義バイナリ)
    BCD_TYPE_SCRIPT = 2  // .bje (スクリプト実行バイナリ)
} BcdFileType;

// バイナリ共通ヘッダ (80 bytes 固定)
typedef struct {
    char     magic[4];           // "JPEN"
    uint16_t version;            // 1
    uint8_t  file_type;          // BcdFileType
    uint8_t  reserved;           // 0
    uint32_t type_meta_offset;   // コンパイラ用型メタデータ開始位置
    uint32_t type_meta_size;     // 型メタデータサイズ
    char     source_file[BCD_MAX_SOURCE_NAME]; // 生みの親ソースファイル名 (例: "RegexParser.cd", "動物.jcd")
} BcdHeader;

typedef enum {
    BCD_VAL_NIL,
    BCD_VAL_BOOL,
    BCD_VAL_INT,
    BCD_VAL_FLOAT,
    BCD_VAL_STRING,
    BCD_VAL_CLASS_REF,
    BCD_VAL_BIGINT,
    BCD_VAL_DECIMAL,
    BCD_VAL_FUNCTION,
} BcdValTag;

const char* get_japeng_lib_dir(void);
void encode_cache_name(const char* name, char* out, size_t max_len);
void get_cache_filepath(const char* base_dir, const char* name, const char* ext, char* out, size_t max_len);

#endif
