#ifndef GC_LAYOUT_H
#define GC_LAYOUT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include "literal.h"
#include "bigint.h"
#include "symbol.h"
#include "object.h"
#include "decimal.h"

// ---------------------------------------------------------
// GCヘッダ用フラグ (ObjHeader.flags)
// ---------------------------------------------------------
#define GC_FLAG_ALLOCATED   0x80  // bit 7: 使用中スロットか
#define GC_FLAG_REMEMBERED  0x08  // bit 3: Remembered Set 登録済みか
#define GC_AGE_MASK         0x06  // bit 1-2: 世代 (0〜3歳)
#define GC_AGE_SHIFT        1
#define GC_AGE_MAX          3     // 3歳で Old 世代へ昇格
#define GC_FLAG_MARK        0x01  // bit 0: 生存マークランプ

// 共通ヘッダ (ジャスト 8バイト / アライメント完全防護)
typedef struct {
    uint8_t  type;       // ObjType (OBJ_INSTANCE, OBJ_LIST, など)
    uint8_t  flags;      // 状態・世代・マークビット
    uint16_t unused;     // 4バイト境界アライメント + 予約
    uint32_t slot_idx;   // ヒープ内のスロット番号
} ObjHeader;

// ① インスタンス: フィールド実体は外部Slab/malloc (32B)
typedef struct {
    ObjHeader header;      // 8B
    void*     klass;       // 8B (ObjClass*)
    uint32_t  field_count; // 4B (フィールド数)
    uint32_t  padding;     // 4B
    Value*    fields;      // 8B (外部バッファポインタ)
} ObjInstance;

// ② 文字列: SSO (21バイト直入れ / 外部Slab) (32B)
#define SSO_MAX_LEN 15

typedef struct {
    ObjHeader header;     // 8B
    bool      is_inline;  // 1B (SSOフラグ)
    uint8_t   length;     // 1B (文字長)
    uint16_t  padding;    // 2B
    uint32_t hash;        // 4B
    union {
        char inline_chars[SSO_MAX_LEN + 1]; // 22B
        struct {
            char*    ptr;      // 8B
            uint32_t capacity; // 4B
            uint32_t unused;   // 6B
        } heap;
    } as;
} ObjString;

static inline const char* string_chars(const ObjString* str) {
    return str->is_inline ? str->as.inline_chars : str->as.heap.ptr;
}

// ③ リスト: 要素バッファは外部Slab/malloc (32B)
typedef struct {
    ObjHeader header;     // 8B
    uint32_t  size;       // 4B (★ size で統一)
    uint32_t  capacity;   // 4B
    Value*    elements;   // 8B (外部バッファポインタ)
    uint64_t  unused;     // 8B
} ObjList;

// ④ クロージャ: キャプチャ配列は外部Slab/malloc (32B)
typedef struct {
    ObjHeader header;        // 8B
    void*     function;      // 8B (ObjFunction*)
    uint32_t  capture_count; // 4B
    uint32_t  padding;       // 4B
    Value*    captures;      // 8B (外部バッファポインタ)
} ObjClosure;

// // ⑤ Decimal: 10進小数 (32B)
// typedef struct {
//     ObjHeader   header;    // 8B
//     BigInteger* mantissa;  // 8B (仮数部)
//     int32_t     scale;     // 4B (小数点位置 >= 0)
//     uint32_t    padding;   // 4B
//     uint64_t    unused;    // 8B
// } ObjDecimal;

// ⑥ 多倍長整数 (32B)
typedef struct {
    ObjHeader header;     // 8B
    bool      sign;       // 1B
    uint8_t   length;     // 1B
    uint16_t  capacity;   // 2B
    uint32_t  unused;     // 4B
    uint32_t* digits;     // 8B
    uint64_t  unused2;    // 8B
} ObjBigInt;

// 32バイト固定長スロット共用体
typedef union Slot {
    ObjHeader   header;
    ObjInstance instance;
    ObjString   string;
    ObjList     list;
    ObjClosure  closure;
    ObjDecimal  decimal;
    ObjBigInt   bigint;
    uint8_t     raw[32];
} Slot;

_Static_assert(sizeof(Slot) == 32, "Slot size must be exactly 32 bytes");

#endif // GC_LAYOUT_H
