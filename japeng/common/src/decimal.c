#include "decimal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// =============================================================================
// 1. 最大公約数 (GCD)
// =============================================================================

// 64bit 整数の高速バイナリ GCD (Stein's Algorithm)
int64_t gcd_i64(int64_t a, int64_t b) 
{
    if (a == 0) return (b >= 0) ? b : -b;
    if (b == 0) return (a >= 0) ? a : -a;

    uint64_t ua = (a == INT64_MIN) ? (uint64_t)INT64_MAX + 1 : (uint64_t)(a < 0 ? -a : a);
    uint64_t ub = (b == INT64_MIN) ? (uint64_t)INT64_MAX + 1 : (uint64_t)(b < 0 ? -b : b);

    int shift = __builtin_ctzll(ua | ub);
    ua >>= __builtin_ctzll(ua);

    do {
        ub >>= __builtin_ctzll(ub);
        if (ua > ub) {
            uint64_t temp = ua;
            ua = ub;
            ub = temp;
        }
        ub -= ua;
    } while (ub != 0);

    return (int64_t)(ua << shift);
}

// BigInteger 同士のユークリッド互除法 GCD
BigInteger* dec_gcd(BigInteger* a, BigInteger* b) 
{
    return bi_gcd(a, b);
}

// =============================================================================
// 2. 内部ヘルパー: 10^n の掛け算
// =============================================================================

// mantissa * 10^n を計算する（スケール合わせ用）
static BigInteger* bi_mul_pow10(BigInteger* a, int32_t n) 
{
    if (n <= 0 || bi_is_zero(a)) return bi_copy(a);

    char* pow_str = (char*)malloc(n + 2);
    pow_str[0] = '1';
    memset(pow_str + 1, '0', n);
    pow_str[n + 1] = '\0';

    BigInteger* p10 = new_bi_from_string(pow_str, n + 1);
    free(pow_str);

    BigInteger* res = bi_mul(a, p10);
    free_bi(p10);
    return res;
}

// =============================================================================
// 3. Decimal 生成・破棄
// =============================================================================

ObjDecimal* new_decimal(BigInteger* mantissa, int32_t scale) 
{
    ObjDecimal* dec = (ObjDecimal*)malloc(sizeof(ObjDecimal));
    dec->header.type = OBJ_DECIMAL;
    dec->mantissa = mantissa;
    dec->scale = (scale < 0) ? 0 : scale;
    return dec;
}

void free_decimal(ObjDecimal* dec) {
    if (!dec) return;
    if (dec->mantissa) {
        free_bi(dec->mantissa);
    }
    free(dec);
}

ObjDecimal* new_decimal_from_i64(int64_t val) 
{
    return new_decimal(bi_from_i64(val), 0);
}

// 文字列リテラル（"123.456", "-0.007"）から生成
ObjDecimal* new_decimal_from_string(const char* str, int len) 
{
    char buf[1024];
    int buf_idx = 0;
    int dot_pos = -1;

    for (int i = 0; i < len; i++) {
        if (str[i] == '.') {
            dot_pos = i;
        } else {
            buf[buf_idx++] = str[i];
        }
    }
    buf[buf_idx] = '\0';

    BigInteger* mantissa = new_bi_from_string(buf, buf_idx);
    int32_t scale = (dot_pos == -1) ? 0 : (len - 1 - dot_pos);

    return new_decimal(mantissa, scale);
}

// 分数（分子 / 分母）から指定精度の Decimal を生成
ObjDecimal* new_decimal_from_fraction(BigInteger* num, BigInteger* den, int32_t precision) 
{
    if (bi_is_zero(den)) return NULL;
    

    // 分子に 10^precision を掛けてから分母で割る
    BigInteger* scaled_num = bi_mul_pow10(num, precision);
    BigInteger* mantissa = bi_div(scaled_num, den);
    free_bi(scaled_num);

    return new_decimal(mantissa, precision);
}

// =============================================================================
// 4. 正規化 (Normalize)
// =============================================================================

// 末尾の無駄な 0 を削って scale を詰める (例: 1.5000 -> 1.5)
ObjDecimal* dec_normalize(ObjDecimal* dec) 
{
    if (!dec || dec->scale == 0 || bi_is_zero(dec->mantissa)) return dec;

    BigInteger* ten = bi_from_i64(10);
    while (dec->scale > 0) {
        BigInteger* rem = bi_mod(dec->mantissa, ten);
        if (!bi_is_zero(rem)) {
            free_bi(rem);
            break; // 割り切れなくなったら終了
        }
        free_bi(rem);

        BigInteger* next_m = bi_div(dec->mantissa, ten);
        free_bi(dec->mantissa);
        dec->mantissa = next_m;
        dec->scale--;
    }
    free_bi(ten);
    return dec;
}

// =============================================================================
// 5. 四則演算
// =============================================================================

// 加算: a + b
ObjDecimal* dec_add(ObjDecimal* a, ObjDecimal* b) 
{
    int32_t max_scale = (a->scale > b->scale) ? a->scale : b->scale;

    BigInteger* ma = bi_mul_pow10(a->mantissa, max_scale - a->scale);
    BigInteger* mb = bi_mul_pow10(b->mantissa, max_scale - b->scale);

    BigInteger* m_res = bi_add(ma, mb);
    free_bi(ma);
    free_bi(mb);

    return dec_normalize(new_decimal(m_res, max_scale));
}

// 減算: a - b
ObjDecimal* dec_sub(ObjDecimal* a, ObjDecimal* b)
{
    int32_t max_scale = (a->scale > b->scale) ? a->scale : b->scale;

    BigInteger* ma = bi_mul_pow10(a->mantissa, max_scale - a->scale);
    BigInteger* mb = bi_mul_pow10(b->mantissa, max_scale - b->scale);

    BigInteger* m_res = bi_sub(ma, mb);
    free_bi(ma);
    free_bi(mb);

    return dec_normalize(new_decimal(m_res, max_scale));
}

// 乗算: a * b
ObjDecimal* dec_mul(ObjDecimal* a, ObjDecimal* b) 
{
    int32_t new_scale = a->scale + b->scale;
    BigInteger* m_res = bi_mul(a->mantissa, b->mantissa);

    return dec_normalize(new_decimal(m_res, new_scale));
}

// 除算: a / b (precision で小数点以下の計算桁数を指定)
ObjDecimal* dec_div(ObjDecimal* a, ObjDecimal* b, int32_t precision) 
{
    // (ma * 10^(precision + scale_b - scale_a)) / mb
    int32_t shift = precision + b->scale - a->scale;
    BigInteger* ma_scaled = NULL;

    if (shift >= 0) {
        ma_scaled = bi_mul_pow10(a->mantissa, shift);
    } else {
        // スケールが余剰な場合は割って合わせる
        BigInteger* p10 = bi_mul_pow10(bi_from_i64(1), -shift);
        ma_scaled = bi_div(a->mantissa, p10);
        free_bi(p10);
    }

    BigInteger* m_res = bi_div(ma_scaled, b->mantissa);
    free_bi(ma_scaled);

    return dec_normalize(new_decimal(m_res, precision));
}

// 比較: a と b の大小判定
int dec_compare(ObjDecimal* a, ObjDecimal* b) {
    int32_t max_scale = (a->scale > b->scale) ? a->scale : b->scale;

    BigInteger* ma = bi_mul_pow10(a->mantissa, max_scale - a->scale);
    BigInteger* mb = bi_mul_pow10(b->mantissa, max_scale - b->scale);

    BigInteger* diff = bi_sub(ma, mb);
    int res = 0;
    if (!bi_is_zero(diff)) {
        res = diff->sign ? 1 : -1;
    }

    free_bi(ma);
    free_bi(mb);
    free_bi(diff);
    return res;
}

char* dec_toString(ObjDecimal* dec, size_t* out_len) 
{
    size_t m_len = 0;
    char* m_str = bi_to_string(dec->mantissa, &m_len);

    if (dec->scale <= 0) {
        if (out_len) *out_len = m_len;
        return m_str;
    }

    bool is_neg = (m_str[0] == '-');
    const char* digits = is_neg ? (m_str + 1) : m_str;
    size_t num_digits = is_neg ? (m_len - 1) : m_len;

    size_t alloc_size = m_len + (size_t)dec->scale + 8;
    char* buf = (char*)malloc(alloc_size);
    size_t out_idx = 0;

    if (is_neg) {
        buf[out_idx++] = '-';
    }

    if (num_digits > (size_t)dec->scale) {
        size_t int_part_len = num_digits - (size_t)dec->scale;
        memcpy(buf + out_idx, digits, int_part_len);
        out_idx += int_part_len;
        buf[out_idx++] = '.';
        memcpy(buf + out_idx, digits + int_part_len, (size_t)dec->scale);
        out_idx += (size_t)dec->scale;
    } else {
        buf[out_idx++] = '0';
        buf[out_idx++] = '.';
        size_t leading_zeros = (size_t)dec->scale - num_digits;
        memset(buf + out_idx, '0', leading_zeros);
        out_idx += leading_zeros;
        memcpy(buf + out_idx, digits, num_digits);
        out_idx += num_digits;
    }

    buf[out_idx] = '\0';
    free(m_str);

    if (out_len) *out_len = out_idx;
    return buf;
}
