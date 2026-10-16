#ifndef DECIMAL_H
#define DECIMAL_H

#include <stdint.h>
#include <stdbool.h>
#include "object.h"
#include "bigint.h"

// Decimal オブジェクト構造体
typedef struct objDecimal {
    Obj header;
    BigInteger* mantissa; // 仮数部（符号・巨大数も BigInteger がそのまま担当）
    int32_t scale;        // 小数点以下の桁数 (>= 0)
} ObjDecimal;

// -----------------------------------------------------------------------------
// 最大公約数 (GCD)
// -----------------------------------------------------------------------------
int64_t     gcd_i64(int64_t a, int64_t b);
BigInteger* dec_gcd(BigInteger* a, BigInteger* b);

// -----------------------------------------------------------------------------
// Decimal 生成・解放
// -----------------------------------------------------------------------------
ObjDecimal* new_decimal(BigInteger* mantissa, int32_t scale);
ObjDecimal* new_decimal_from_string(const char* str, int len);
ObjDecimal* new_decimal_from_i64(int64_t val);
ObjDecimal* new_decimal_from_fraction(BigInteger* num, BigInteger* den, int32_t precision);
void        free_decimal(ObjDecimal* dec);

// -----------------------------------------------------------------------------
// Decimal 演算
// -----------------------------------------------------------------------------
ObjDecimal* dec_normalize(ObjDecimal* dec); // 末尾の不要な 0 を削って scale を最適化
ObjDecimal* dec_add(ObjDecimal* a, ObjDecimal* b);
ObjDecimal* dec_sub(ObjDecimal* a, ObjDecimal* b);
ObjDecimal* dec_mul(ObjDecimal* a, ObjDecimal* b);
ObjDecimal* dec_div(ObjDecimal* a, ObjDecimal* b, int32_t precision);
int         dec_compare(ObjDecimal* a, ObjDecimal* b); // -1: a < b, 0: a == b, 1: a > b
ObjString* dec_toString(ObjDecimal* dec);

#endif // DECIMAL_H
