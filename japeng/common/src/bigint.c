#include "common.h"

// -----------------------------------------------------------------------------
// 基本ライフサイクル
// -----------------------------------------------------------------------------
BigInteger* new_BigInteger(size_t init_capa) 
{
    if (init_capa == 0) init_capa = 1;
    BigInteger* bi = (BigInteger*)malloc(sizeof(BigInteger) + sizeof(uint32_t) * init_capa);
    bi->header.type = OBJ_BIGINTEGER; // OBJ_BIGINTEGER に差し替え
    bi->length = 0;
    bi->capacity = init_capa;
    bi->sign = true;
    return bi;
}

void free_bi(BigInteger* bi) 
{
    if (bi != NULL) {
        free(bi);
    }
}

// 最上位の余分な 0 を削る（0 の場合は length = 1, nums[0] = 0, sign = true に正規化）
static void trim(BigInteger* bi) 
{
    while (bi->length > 1 && bi->nums[bi->length - 1] == 0) {
        bi->length--;
    }
    if (bi->length == 1 && bi->nums[0] == 0) {
        bi->sign = true;
    }
}

// -----------------------------------------------------------------------------
// 昇格・降格（SmallInteger <-> BigInteger）
// -----------------------------------------------------------------------------

// 64bit 整数からの昇格
BigInteger* bi_from_i64(int64_t val) 
{
    BigInteger* bi = new_BigInteger(2);
    if (val < 0) {
        bi->sign = false;
        val = -(uint64_t)val; // INT64_MIN 対策
    } else {
        bi->sign = true;
    }

    uint64_t uval = (uint64_t)val;
    bi->nums[0] = (uint32_t)(uval & 0xFFFFFFFF);
    uint32_t high = (uint32_t)(uval >> 32);

    if (high > 0) {
        bi->nums[1] = high;
        bi->length = 2;
    } else {
        bi->length = 1;
    }
    return bi;
}

// 32bit 符号付き整数の範囲に収まるなら降格して SmallInteger(Value) を返す
Value bi_to_value_and_demote(BigInteger* bi) 
{
    trim(bi);

    if (bi->length == 1) {
        uint32_t u = bi->nums[0];
        
        // 正の数: 0 〜 INT32_MAX
        if (bi->sign && u <= (uint32_t)INT32_MAX) {
            int32_t val = (int32_t)u;
            free_bi(bi);
            return make_int(val);
        }
        // 負の数: -1 〜 -INT32_MAX, および INT32_MIN
        if (!bi->sign) {
            if (u <= (uint32_t)INT32_MAX) {
                int32_t val = -(int32_t)u;
                free_bi(bi);
                return make_int(val);
            } else if (u == 0x80000000U) { // INT32_MIN (-2147483648)
                free_bi(bi);
                return make_int(INT32_MIN);
            }
        }
    }

    return make_obj((Obj*)bi);
}

// -----------------------------------------------------------------------------
// 比較演算
// -----------------------------------------------------------------------------

// 絶対値の比較 (|lhs| > |rhs| なら 1, |lhs| < |rhs| なら -1, 一致なら 0)
int bi_compare_abs(const BigInteger* lhs, const BigInteger* rhs) 
{
    if (lhs->length != rhs->length) {
        return (lhs->length > rhs->length) ? 1 : -1;
    }

    for (int idx = (int)lhs->length - 1; idx >= 0; idx--) {
        if (lhs->nums[idx] == rhs->nums[idx]) continue;
        return (lhs->nums[idx] > rhs->nums[idx]) ? 1 : -1;
    }

    return 0;
}

// 符号込みの比較 (lhs > rhs なら 1, lhs < rhs なら -1, 一致なら 0)
int compare_bi(const BigInteger* lhs, const BigInteger* rhs) 
{
    if (lhs->sign != rhs->sign) {
        return lhs->sign ? 1 : -1;
    }

    if (lhs->length != rhs->length) {
        if (lhs->sign) return (lhs->length > rhs->length) ? 1 : -1;
        else return (lhs->length < rhs->length) ? 1 : -1;
    }

    for (int idx = (int)lhs->length - 1; idx >= 0; idx--) {
        if (lhs->nums[idx] == rhs->nums[idx]) continue;
        bool greater = lhs->nums[idx] > rhs->nums[idx];
        return (lhs->sign == greater) ? 1 : -1;
    }

    return 0;
}

// -----------------------------------------------------------------------------
// シフト演算
// -----------------------------------------------------------------------------

BigInteger* bi_shl(BigInteger* a, size_t shift_bits) 
{
    if (shift_bits == 0) {
        BigInteger* res = new_BigInteger(a->length);
        memcpy(res->nums, a->nums, sizeof(uint32_t) * a->length);
        res->length = a->length;
        res->sign = a->sign;
        return res;
    }

    size_t word_shift = shift_bits / 32;
    int bit_shift = (int)(shift_bits % 32);

    size_t max_len = a->length + word_shift + 1;
    BigInteger* res = new_BigInteger(max_len);
    res->sign = a->sign;

    memset(res->nums, 0, sizeof(uint32_t) * word_shift);

    if (bit_shift == 0) {
        memcpy(res->nums + word_shift, a->nums, sizeof(uint32_t) * a->length);
        res->length = a->length + word_shift;
    } else {
        uint32_t carry = 0;
        for (size_t i = 0; i < a->length; i++) {
            uint64_t cur = ((uint64_t)a->nums[i] << bit_shift) | carry;
            res->nums[word_shift + i] = (uint32_t)cur;
            carry = (uint32_t)(cur >> 32);
        }
        size_t final_len = a->length + word_shift;
        if (carry > 0) {
            res->nums[final_len++] = carry;
        }
        res->length = final_len;
    }

    trim(res);
    return res;
}

BigInteger* bi_shr(BigInteger* a, size_t shift_bits) 
{
    size_t word_shift = shift_bits / 32;
    int bit_shift = (int)(shift_bits % 32);

    if (word_shift >= a->length) {
        BigInteger* res = new_BigInteger(1);
        res->nums[0] = 0;
        res->length = 1;
        res->sign = true;
        return res;
    }

    size_t new_len = a->length - word_shift;
    BigInteger* res = new_BigInteger(new_len);
    res->sign = a->sign;

    if (bit_shift == 0) {
        memcpy(res->nums, a->nums + word_shift, sizeof(uint32_t) * new_len);
        res->length = new_len;
    } else {
        uint32_t carry = 0;
        for (int i = (int)a->length - 1; i >= (int)word_shift; i--) {
            uint64_t cur = ((uint64_t)carry << 32) | a->nums[i];
            res->nums[i - word_shift] = (uint32_t)(cur >> bit_shift);
            carry = a->nums[i] & ((1U << bit_shift) - 1);
        }
        res->length = new_len;
    }

    trim(res);
    return res;
}

// -----------------------------------------------------------------------------
// 加減算
// -----------------------------------------------------------------------------

// 前提: lhs->length >= rhs->length
static BigInteger* bi_add_abs(BigInteger* lhs, BigInteger* rhs) 
{
    BigInteger* result = new_BigInteger(lhs->length + 1);
    uint64_t carry = 0;
    size_t idx = 0;

    for (; idx < rhs->length; idx++) {
        uint64_t res = (uint64_t)lhs->nums[idx] + (uint64_t)rhs->nums[idx] + carry;
        result->nums[idx] = (uint32_t)res;
        carry = res >> 32;
    }

    for (; idx < lhs->length; idx++) {
        uint64_t res = (uint64_t)lhs->nums[idx] + carry;
        result->nums[idx] = (uint32_t)res;
        carry = res >> 32;
    }

    if (carry > 0) {
        result->nums[idx++] = (uint32_t)carry;
    }

    result->length = idx;
    result->sign = true;
    return result;
}

// 前提: bi_compare_abs(lhs, rhs) >= 0
static BigInteger* bi_sub_abs(BigInteger* lhs, BigInteger* rhs) 
{
    BigInteger* result = new_BigInteger(lhs->length);
    uint64_t borrow = 0;
    size_t idx = 0;

    for (; idx < rhs->length; idx++) {
        uint64_t l = lhs->nums[idx];
        uint64_t r = (uint64_t)rhs->nums[idx] + borrow;

        if (l < r) {
            result->nums[idx] = (uint32_t)((l + (1ULL << 32)) - r);
            borrow = 1;
        } else {
            result->nums[idx] = (uint32_t)(l - r);
            borrow = 0;
        }
    }

    for (; idx < lhs->length; idx++) {
        uint64_t l = lhs->nums[idx];
        if (l < borrow) {
            result->nums[idx] = (uint32_t)((l + (1ULL << 32)) - borrow);
            borrow = 1;
        } else {
            result->nums[idx] = (uint32_t)(l - borrow);
            borrow = 0;
        }
    }

    result->length = lhs->length;
    result->sign = true;
    trim(result);
    return result;
}

BigInteger* bi_add(BigInteger* lhs, BigInteger* rhs) 
{
    if (lhs->sign == rhs->sign) {
        BigInteger* res;
        if (bi_compare_abs(lhs, rhs) >= 0) {
            res = bi_add_abs(lhs, rhs);
        } else {
            res = bi_add_abs(rhs, lhs);
        }
        res->sign = lhs->sign;
        return res;
    }

    if (bi_compare_abs(lhs, rhs) >= 0) {
        BigInteger* res = bi_sub_abs(lhs, rhs);
        res->sign = lhs->sign;
        return res;
    } else {
        BigInteger* res = bi_sub_abs(rhs, lhs);
        res->sign = rhs->sign;
        return res;
    }
}

BigInteger* bi_sub(BigInteger* lhs, BigInteger* rhs) 
{
    if (lhs->sign != rhs->sign) {
        BigInteger* res;
        if (bi_compare_abs(lhs, rhs) >= 0) {
            res = bi_add_abs(lhs, rhs);
        } else {
            res = bi_add_abs(rhs, lhs);
        }
        res->sign = lhs->sign;
        return res;
    }

    if (bi_compare_abs(lhs, rhs) >= 0) {
        BigInteger* res = bi_sub_abs(lhs, rhs);
        res->sign = lhs->sign;
        return res;
    } else {
        BigInteger* res = bi_sub_abs(rhs, lhs);
        res->sign = !lhs->sign;
        return res;
    }
}

// -----------------------------------------------------------------------------
// 乗算
// -----------------------------------------------------------------------------

static BigInteger* bi_mul_abs(BigInteger* lhs, BigInteger* rhs) 
{
    size_t res_len = lhs->length + rhs->length;
    BigInteger* result = new_BigInteger(res_len);
    memset(result->nums, 0, sizeof(uint32_t) * res_len);

    for (size_t i = 0; i < lhs->length; i++) {
        uint64_t carry = 0;
        for (size_t j = 0; j < rhs->length; j++) {
            uint64_t cur = (uint64_t)result->nums[i + j]
                         + (uint64_t)lhs->nums[i] * rhs->nums[j]
                         + carry;

            result->nums[i + j] = (uint32_t)cur;
            carry = cur >> 32;
        }
        result->nums[i + rhs->length] += (uint32_t)carry;
    }

    result->length = res_len;
    result->sign = true;
    trim(result);
    return result;
}

BigInteger* bi_mul(BigInteger* lhs, BigInteger* rhs) 
{
    BigInteger* res = bi_mul_abs(lhs, rhs);
    res->sign = !(lhs->sign ^ rhs->sign);
    if (res->length == 1 && res->nums[0] == 0) res->sign = true;
    return res;
}

// -----------------------------------------------------------------------------
// 除算・剰余 (Knuth Algorithm D)
// -----------------------------------------------------------------------------

static void bi_divmod_abs(BigInteger* u, BigInteger* v, BigInteger** out_q, BigInteger** out_r) 
{
    int cmp = bi_compare_abs(u, v);
    if (cmp < 0) {
        BigInteger* q = new_BigInteger(1);
        q->nums[0] = 0;
        q->length = 1;
        q->sign = true;

        BigInteger* r = new_BigInteger(u->length);
        memcpy(r->nums, u->nums, sizeof(uint32_t) * u->length);
        r->length = u->length;
        r->sign = true;

        *out_q = q;
        *out_r = r;
        return;
    }

    if (v->length == 1) {
        uint32_t divisor = v->nums[0];
        BigInteger* q = new_BigInteger(u->length);
        uint64_t rem = 0;

        for (int i = (int)u->length - 1; i >= 0; i--) {
            uint64_t cur = (rem << 32) | u->nums[i];
            q->nums[i] = (uint32_t)(cur / divisor);
            rem = cur % divisor;
        }

        q->length = u->length;
        q->sign = true;
        trim(q);

        BigInteger* r = new_BigInteger(1);
        r->nums[0] = (uint32_t)rem;
        r->length = 1;
        r->sign = true;

        *out_q = q;
        *out_r = r;
        return;
    }

    size_t n = v->length;
    size_t m = u->length - n;

    int shift = __builtin_clz(v->nums[n - 1]);
    BigInteger* un = bi_shl(u, shift);
    BigInteger* vn = bi_shl(v, shift);

    if (un->length == u->length) {
        // grow(un);
        un->nums[un->length++] = 0;
    }

    BigInteger* q = new_BigInteger(m + 1);
    q->length = m + 1;
    q->sign = true;

    for (int j = (int)m; j >= 0; j--) {
        uint64_t u_high = ((uint64_t)un->nums[j + n] << 32) | un->nums[j + n - 1];
        uint64_t qhat = u_high / vn->nums[n - 1];
        uint64_t rhat = u_high % vn->nums[n - 1];

        while (qhat >= (1ULL << 32) ||
               (qhat * vn->nums[n - 2] > ((rhat << 32) | un->nums[j + n - 2]))) {
            qhat--;
            rhat += vn->nums[n - 1];
            if (rhat >= (1ULL << 32)) break;
        }

        int64_t borrow = 0;
        for (size_t i = 0; i < n; i++) {
            uint64_t p = qhat * vn->nums[i];
            int64_t diff = (int64_t)un->nums[j + i] - borrow - (uint32_t)p;
            un->nums[j + i] = (uint32_t)diff;
            borrow = (p >> 32) - (diff >> 32);
        }
        int64_t diff = (int64_t)un->nums[j + n] - borrow;
        un->nums[j + n] = (uint32_t)diff;

        if (diff < 0) {
            qhat--;
            uint64_t carry = 0;
            for (size_t i = 0; i < n; i++) {
                uint64_t sum = (uint64_t)un->nums[j + i] + vn->nums[i] + carry;
                un->nums[j + i] = (uint32_t)sum;
                carry = sum >> 32;
            }
            un->nums[j + n] += (uint32_t)carry;
        }

        q->nums[j] = (uint32_t)qhat;
    }

    trim(q);

    un->length = n;
    trim(un);
    BigInteger* r = bi_shr(un, shift);

    free_bi(un);
    free_bi(vn);

    *out_q = q;
    *out_r = r;
}

BigInteger* bi_div(BigInteger* lhs, BigInteger* rhs) 
{
    BigInteger *q, *r;
    bi_divmod_abs(lhs, rhs, &q, &r);
    free_bi(r);
    q->sign = !(lhs->sign ^ rhs->sign);
    if (q->length == 1 && q->nums[0] == 0) q->sign = true;
    return q;
}

BigInteger* bi_mod(BigInteger* lhs, BigInteger* rhs) 
{
    BigInteger *q, *r;
    bi_divmod_abs(lhs, rhs, &q, &r);
    free_bi(q);
    r->sign = lhs->sign;
    if (r->length == 1 && r->nums[0] == 0) r->sign = true;
    return r;
}

// -----------------------------------------------------------------------------
// 文字列からの変換 (Base 10 -> Base 2^32)
// -----------------------------------------------------------------------------

static void mul_add_chunk(BigInteger* bi, uint32_t base_mul, uint32_t chunk) 
{
    uint64_t carry = chunk;
    for (size_t i = 0; i < bi->length; i++) {
        uint64_t cur = (uint64_t)bi->nums[i] * base_mul + carry;
        bi->nums[i] = (uint32_t)cur;
        carry = cur >> 32;
    }
    if (carry > 0) {
        // grow(bi);
        bi->nums[bi->length++] = (uint32_t)carry;
    }
}

BigInteger* new_bi_from_string(Symbol* string) 
{
    int start = 0;
    bool sign = true;
    if (string->length > 0 && (string->chars[0] == '+' || string->chars[0] == '-')) {
        sign = (string->chars[0] == '+');
        start = 1;
    }

    int total_digits = string->length - start;
    if (total_digits <= 0) {
        BigInteger* bi = new_BigInteger(1);
        bi->length = 1;
        bi->nums[0] = 0;
        return bi;
    }

    // 溢れない上限ワード数を事前計算して一括確保
    size_t init_capa = (size_t)(total_digits / 9) + 2;
    BigInteger* bi = new_BigInteger(init_capa);
    bi->sign = sign;
    bi->length = 1;
    bi->nums[0] = 0;

    int first_chunk_len = total_digits % 9;
    if (first_chunk_len == 0) first_chunk_len = 9;

    int idx = start;
    bool is_first = true;

    while (idx < string->length) {
        int chunk_len = is_first ? first_chunk_len : 9;
        is_first = false;

        uint32_t chunk = 0;
        uint32_t base_mul = 1;

        for (int i = 0; i < chunk_len; i++) {
            chunk = chunk * 10 + (string->chars[idx++] - '0');
            base_mul *= 10;
        }

        mul_add_chunk(bi, base_mul, chunk);
    }

    trim(bi);
    return bi;
}

// 必要な最大文字数を見積もるヘルパー
// 2^32 ≈ 4.29 * 10^9 なので、1ワードあたり最大 10 桁 + 符号 + 終端ヌル文字
static size_t estimate_max_decimal_digits(size_t length) {
    return length * 10 + 2; 
}

Symbol* bi_to_string(const BigInteger* bi) 
{
    // 1. 特殊ケース: 0 の場合
    if (bi->length == 1 && bi->nums[0] == 0) {
        return new_string("0", 1);
    }

    // 破壊的除算を避けるため、作業用コピーを作成
    BigInteger* copy = new_BigInteger(bi->length);
    memcpy(copy->nums, bi->nums, sizeof(uint32_t) * bi->length);
    copy->length = bi->length;
    copy->sign = true;

    // 除数 10^9 を表す 1 ワードの BigInteger
    BigInteger* base = new_BigInteger(1);
    base->nums[0] = 1000000000U;
    base->length = 1;
    base->sign = true;

    // 9桁ごとの chunk を格納する一時バッファ（下位 chunk から順に積まれる）
    size_t max_chunks = (bi->length * 10 / 9) + 2;
    uint32_t* chunks = (uint32_t*)malloc(sizeof(uint32_t) * max_chunks);
    size_t chunk_count = 0;

    // 2. 10^9 で繰り返し割り算し、余り（9桁チャンク）を取り出す
    while (!(copy->length == 1 && copy->nums[0] == 0)) {
        BigInteger *q, *r;
        bi_divmod_abs(copy, base, &q, &r);

        chunks[chunk_count++] = r->nums[0]; // 余り (0 〜 999999999)

        free_bi(copy);
        free_bi(r);
        copy = q; // 商を作業用変数へ更新
    }

    free_bi(copy);
    free_bi(base);

    // 3. 取り出した chunk を文字列バッファへ逆順（上位桁から）に整形
    size_t buf_size = estimate_max_decimal_digits(bi->length);
    char* buf = (char*)malloc(buf_size);
    size_t out_idx = 0;

    // 負数なら先頭にマイナスを付与
    if (!bi->sign) {
        buf[out_idx++] = '-';
    }

    // 最上位 chunk（先頭の余分な 0 パディングは不要）
    out_idx += sprintf(buf + out_idx, "%u", chunks[chunk_count - 1]);

    // 2番目以降の chunk（9桁に満たない場合は 0 で埋める: "%09u"）
    for (int i = (int)chunk_count - 2; i >= 0; i--) {
        out_idx += sprintf(buf + out_idx, "%09u", chunks[i]);
    }

    buf[out_idx] = '\0';
    free(chunks);

    // 言語の Symbol として wrap
    Symbol* result_str = new_string(buf, (int)out_idx);
    free(buf);

    return result_str;
}

bool bi_is_zero(const BigInteger* bi) 
{
    if (bi == NULL) return true;
    return (bi->length == 0) || (bi->length == 1 && bi->nums[0] == 0);
}

// ディープコピー
BigInteger* bi_copy(const BigInteger* bi) 
{
    if (bi == NULL) return NULL;
    BigInteger* copy = new_BigInteger(bi->length);
    copy->length = bi->length;
    copy->sign = bi->sign;
    memcpy(copy->nums, bi->nums, sizeof(uint32_t) * bi->length);
    return copy;
}

// 絶対値コピー（符号を常に正にする）
BigInteger* bi_abs_copy(const BigInteger* bi) 
{
    BigInteger* copy = bi_copy(bi);
    if (copy != NULL) {
        copy->sign = true;
    }
    return copy;
}

// =============================================================================
// べき乗 (繰り返し自乗法: Exponentiation by squaring)
// =============================================================================
BigInteger* bi_pow(const BigInteger* base, uint64_t exp) 
{
    if (exp == 0) {
        return bi_from_i64(1);
    }
    if (bi_is_zero(base)) {
        return bi_from_i64(0);
    }

    BigInteger* result = bi_from_i64(1);
    BigInteger* cur = bi_copy(base);

    while (exp > 0) {
        if (exp & 1) {
            BigInteger* next_res = bi_mul(result, cur);
            free_bi(result);
            result = next_res;
        }
        exp >>= 1;
        if (exp > 0) {
            BigInteger* next_cur = bi_mul(cur, cur);
            free_bi(cur);
            cur = next_cur;
        }
    }

    free_bi(cur);
    return result;
}

// =============================================================================
// 高速バイナリ GCD (Stein's Algorithm for BigInteger)
// =============================================================================

// 末尾の連続する 0 ビット数をカウント (Trailing Zeros)
static size_t bi_ctz(const BigInteger* a) 
{
    if (bi_is_zero(a)) return 0;
    size_t count = 0;
    for (size_t i = 0; i < a->length; i++) {
        if (a->nums[i] == 0) {
            count += 32;
        } else {
            count += (size_t)__builtin_ctz(a->nums[i]);
            break;
        }
    }
    return count;
}

// インプレース右シフト (メモリ確保なしで直接ビットシフト)
static void bi_shr_inplace(BigInteger* a, size_t shift_bits) 
{
    if (shift_bits == 0 || bi_is_zero(a)) return;

    size_t word_shift = shift_bits / 32;
    int bit_shift = (int)(shift_bits % 32);

    if (word_shift >= a->length) {
        a->nums[0] = 0;
        a->length = 1;
        a->sign = true;
        return;
    }

    size_t new_len = a->length - word_shift;

    if (bit_shift == 0) {
        memmove(a->nums, a->nums + word_shift, sizeof(uint32_t) * new_len);
    } else {
        // 下位ワードから上位を参照して安全に詰める
        for (size_t j = 0; j < new_len; j++) {
            uint32_t low = a->nums[j + word_shift] >> bit_shift;
            uint32_t high = (j + word_shift + 1 < a->length) 
                            ? (a->nums[j + word_shift + 1] << (32 - bit_shift)) 
                            : 0;
            a->nums[j] = low | high;
        }
    }
    a->length = new_len;
    trim(a);
}

// インプレース減算: a = a - b (前提: a >= b >= 0)
static void bi_sub_inplace(BigInteger* a, const BigInteger* b) 
{
    uint64_t borrow = 0;
    for (size_t i = 0; i < b->length; i++) {
        uint64_t l = a->nums[i];
        uint64_t r = (uint64_t)b->nums[i] + borrow;
        if (l < r) {
            a->nums[i] = (uint32_t)((l + (1ULL << 32)) - r);
            borrow = 1;
        } else {
            a->nums[i] = (uint32_t)(l - r);
            borrow = 0;
        }
    }
    for (size_t i = b->length; i < a->length; i++) {
        uint64_t l = a->nums[i];
        if (l < borrow) {
            a->nums[i] = (uint32_t)((l + (1ULL << 32)) - borrow);
            borrow = 1;
        } else {
            a->nums[i] = (uint32_t)(l - borrow);
            borrow = 0;
        }
    }
    trim(a);
}

// バイナリ GCD 本体
BigInteger* bi_gcd(const BigInteger* a, const BigInteger* b) 
{
    if (bi_is_zero(a)) return bi_abs_copy(b);
    if (bi_is_zero(b)) return bi_abs_copy(a);

    BigInteger* u = bi_abs_copy(a);
    BigInteger* v = bi_abs_copy(b);

    // 1. 共通の 2 の因数 2^k を抽出
    size_t u_shift = bi_ctz(u);
    size_t v_shift = bi_ctz(v);
    size_t common_shift = (u_shift < v_shift) ? u_shift : v_shift;

    // 2. u と v を奇数にする
    bi_shr_inplace(u, u_shift);
    bi_shr_inplace(v, v_shift);

    // 3. ループ（除算を一切使わず、減算とビットシフトだけで収束）
    while (!bi_is_zero(v)) {
        int cmp = bi_compare_abs(u, v);
        if (cmp > 0) {
            // u > v の場合スワップして常に u <= v を維持
            BigInteger* tmp = u;
            u = v;
            v = tmp;
        } else if (cmp == 0) {
            break;
        }

        // v = v - u (奇数 - 奇数 なので結果は必ず偶数！)
        bi_sub_inplace(v, u);

        // v の末尾ゼロをすべて右シフトして奇数に戻す
        size_t s = bi_ctz(v);
        bi_shr_inplace(v, s);
    }

    // 4. 共通の 2^k を掛け戻す (左シフト)
    BigInteger* result = bi_shl(u, common_shift);
    result->sign = true;

    free_bi(u);
    free_bi(v);

    return result;
}
