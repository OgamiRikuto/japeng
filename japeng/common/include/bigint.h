#ifndef BIGINT_H
#define BIGINT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

#include "object.h"

typedef struct bi {
    Obj header;
    size_t length;
    size_t capacity;
    bool sign; // true: 正(+), false: 負(-)
    uint32_t nums[];
} BigInteger;

BigInteger* new_BigInteger(size_t init_capa);

void free_bi(BigInteger* bi);

// 64bit 整数からの昇格
BigInteger* bi_from_i64(int64_t val);

// 32bit 符号付き整数の範囲に収まるなら降格して SmallInteger(Value) を返す
Value bi_to_value_and_demote(BigInteger* bi);

// 絶対値の比較 (|lhs| > |rhs| なら 1, |lhs| < |rhs| なら -1, 一致なら 0)
int bi_compare_abs(const BigInteger* lhs, const BigInteger* rhs);

// 符号込みの比較 (lhs > rhs なら 1, lhs < rhs なら -1, 一致なら 0)
int compare_bi(const BigInteger* lhs, const BigInteger* rhs);

BigInteger* bi_shl(BigInteger* a, size_t shift_bits);
BigInteger* bi_shr(BigInteger* a, size_t shift_bits);
BigInteger* bi_add(BigInteger* lhs, BigInteger* rhs);
BigInteger* bi_sub(BigInteger* lhs, BigInteger* rhs);
BigInteger* bi_mul(BigInteger* lhs, BigInteger* rhs);
BigInteger* bi_div(BigInteger* lhs, BigInteger* rhs);
BigInteger* bi_mod(BigInteger* lhs, BigInteger* rhs);
BigInteger* bi_pow(const BigInteger* base, uint64_t exp);
BigInteger* bi_gcd(const BigInteger* a, const BigInteger* b);
BigInteger* new_bi_from_string(Symbol* string);
Symbol* bi_to_string(const BigInteger* bi);

bool        bi_is_zero(const BigInteger* bi);
BigInteger* bi_copy(const BigInteger* bi);
BigInteger* bi_abs_copy(const BigInteger* bi);

#endif
