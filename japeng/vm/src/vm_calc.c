#include "vm_calc.h"
#include "bigint.h"
#include "decimal.h"
#include "object.h"
#include "gc.h"
#include "error.h"

// -----------------------------------------------------------------------------
// 高速パス: 最適化整数演算
// -----------------------------------------------------------------------------
Value calc_add_int(int32_t a, int32_t b) {
    int32_t res;
    if (!__builtin_add_overflow(a, b, &res)) {
        return make_int(res);
    }
    int64_t wide = (int64_t)a + (int64_t)b;
    return make_obj((Obj*)bi_from_i64(wide));
}

Value calc_add_int_slow(Value a, Value b) {
    BigInteger* ba = is_obj_type(a, OBJ_BIGINTEGER) ? (BigInteger*)as_obj(a) : bi_from_i64(as_int(a));
    BigInteger* bb = is_obj_type(b, OBJ_BIGINTEGER) ? (BigInteger*)as_obj(b) : bi_from_i64(as_int(b));

    BigInteger* res = bi_add(ba, bb);

    if (!is_obj_type(a, OBJ_BIGINTEGER)) free_bi(ba);
    if (!is_obj_type(b, OBJ_BIGINTEGER)) free_bi(bb);

    return bi_to_value_and_demote(res);
}

Value calc_sub_int(int32_t a, int32_t b) {
    int32_t res;
    if (!__builtin_sub_overflow(a, b, &res)) {
        return make_int(res);
    }
    int64_t wide = (int64_t)a - (int64_t)b;
    return make_obj((Obj*)bi_from_i64(wide));
}

Value calc_sub_int_slow(Value a, Value b) {
    BigInteger* ba = is_obj_type(a, OBJ_BIGINTEGER) ? (BigInteger*)as_obj(a) : bi_from_i64(as_int(a));
    BigInteger* bb = is_obj_type(b, OBJ_BIGINTEGER) ? (BigInteger*)as_obj(b) : bi_from_i64(as_int(b));

    BigInteger* res = bi_sub(ba, bb);

    if (!is_obj_type(a, OBJ_BIGINTEGER)) free_bi(ba);
    if (!is_obj_type(b, OBJ_BIGINTEGER)) free_bi(bb);

    return bi_to_value_and_demote(res);
}

Value calc_mul_int(int32_t a, int32_t b) {
    int32_t res;
    if (!__builtin_mul_overflow(a, b, &res)) {
        return make_int(res);
    }
    int64_t wide = (int64_t)a * (int64_t)b;
    return make_obj((Obj*)bi_from_i64(wide));
}

Value calc_mul_int_slow(Value a, Value b) {
    BigInteger* ba = is_obj_type(a, OBJ_BIGINTEGER) ? (BigInteger*)as_obj(a) : bi_from_i64(as_int(a));
    BigInteger* bb = is_obj_type(b, OBJ_BIGINTEGER) ? (BigInteger*)as_obj(b) : bi_from_i64(as_int(b));

    BigInteger* res = bi_mul(ba, bb);

    if (!is_obj_type(a, OBJ_BIGINTEGER)) free_bi(ba);
    if (!is_obj_type(b, OBJ_BIGINTEGER)) free_bi(bb);

    return bi_to_value_and_demote(res);
}

Value calc_div_int(int32_t a, int32_t b) {
    if (b == 0) {
        error_runtime(ERR_DIVISION_BY_ZERO);
        return make_nil();
    }
    if (a == INT32_MIN && b == -1) {
        int64_t wide = -(int64_t)INT32_MIN;
        return make_obj((Obj*)bi_from_i64(wide));
    }
    return make_int(a / b);
}

Value calc_div_int_slow(Value a, Value b) {
    BigInteger* ba = is_obj_type(a, OBJ_BIGINTEGER) ? (BigInteger*)as_obj(a) : bi_from_i64(as_int(a));
    BigInteger* bb = is_obj_type(b, OBJ_BIGINTEGER) ? (BigInteger*)as_obj(b) : bi_from_i64(as_int(b));

    BigInteger* res = bi_div(ba, bb);

    if (!is_obj_type(a, OBJ_BIGINTEGER)) free_bi(ba);
    if (!is_obj_type(b, OBJ_BIGINTEGER)) free_bi(bb);

    return bi_to_value_and_demote(res);
}

Value calc_rem_int(int32_t a, int32_t b) {
    if (b == 0) {
        error_runtime(ERR_DIVISION_BY_ZERO);
        return make_nil();
    }
    return make_int(a % b);
}

Value calc_rem_int_slow(Value a, Value b) {
    BigInteger* ba = is_obj_type(a, OBJ_BIGINTEGER) ? (BigInteger*)as_obj(a) : bi_from_i64(as_int(a));
    BigInteger* bb = is_obj_type(b, OBJ_BIGINTEGER) ? (BigInteger*)as_obj(b) : bi_from_i64(as_int(b));

    BigInteger* res = bi_mod(ba, bb);

    if (!is_obj_type(a, OBJ_BIGINTEGER)) free_bi(ba);
    if (!is_obj_type(b, OBJ_BIGINTEGER)) free_bi(bb);

    return bi_to_value_and_demote(res);
}

int calc_compare_int_slow(Value a, Value b) 
{
    // SmallInteger 側があれば一時的に BigInteger 化
    BigInteger* ba = is_obj_type(a, OBJ_BIGINTEGER) 
                     ? (BigInteger*)as_obj(a) 
                     : bi_from_i64(as_int(a));
    BigInteger* bb = is_obj_type(b, OBJ_BIGINTEGER) 
                     ? (BigInteger*)as_obj(b) 
                     : bi_from_i64(as_int(b));

    // 既存の compare_bi で多倍長比較
    int res = compare_bi(ba, bb);

    // 一時生成したオブジェクトを解放
    if (!is_obj_type(a, OBJ_BIGINTEGER)) free_bi(ba);
    if (!is_obj_type(b, OBJ_BIGINTEGER)) free_bi(bb);

    return res;
}
// -----------------------------------------------------------------------------
// 汎用加算: SmallInt / BigInt / Float / Decimal / String
// -----------------------------------------------------------------------------
Value calc_add(Value a, Value b) {

    // 未実装
    // if (is_obj_type(a, OBJ_STRING) && is_obj_type(b, OBJ_STRING)) {
    //     ObjString* sa = as_string(a);
    //     ObjString* sb = as_string(b);
    //     // 文字列結合関数（既存の連結処理）
    //     return make_obj((Obj*)concat_strings(sa, sb));
    // }

    // Decimal 演算 (どちらかが Decimal なら Decimal として加算)
    if (is_obj_type(a, OBJ_DECIMAL) || is_obj_type(b, OBJ_DECIMAL)) {
        ObjDecimal* da = is_obj_type(a, OBJ_DECIMAL) ? as_decimal(a) : new_decimal_from_i64(as_int(a));
        ObjDecimal* db = is_obj_type(b, OBJ_DECIMAL) ? as_decimal(b) : new_decimal_from_i64(as_int(b));

        ObjDecimal* res = dec_add(da, db);

        if (!is_obj_type(a, OBJ_DECIMAL)) free_decimal(da);
        if (!is_obj_type(b, OBJ_DECIMAL)) free_decimal(db);

        return make_obj((Obj*)res);
    }

    // 4. Float 演算
    if (is_float(a) || is_float(b)) {
        double fa = is_float(a) ? as_float(a) : (double)as_int(a);
        double fb = is_float(b) ? as_float(b) : (double)as_int(b);
        return make_float(fa + fb);
    }

    error_runtime(ERR_TYPE_OP_ADD);
    return make_nil();
}

// 三値比較: a < b なら -1, a == b なら 0, a > b なら 1
int calc_compare(Value a, Value b) 
{
    if (a == b) return 0;

    // 片方だけが nil の場合、絶対に一致しない
    if (is_nil(a) || is_nil(b)) return (is_nil(a) ? -1 : 1);
    
    // 1. Float 同士
    if (is_float(a) && is_float(b)) {
        double fa = as_float(a);
        double fb = as_float(b);
        if (fa < fb) return -1;
        if (fa > fb) return 1;
        return 0;
    }

    // 2. Decimal 同士
    if (is_obj_type(a, OBJ_DECIMAL) && is_obj_type(b, OBJ_DECIMAL)) {
        return dec_compare(as_decimal(a), as_decimal(b));
    }

    // 3. String 同士 (辞書順比較)
    if (is_obj_type(a, OBJ_STRING) && is_obj_type(b, OBJ_STRING)) {
        ObjString* sa = as_string(a);
        ObjString* sb = as_string(b);
        int min_len = (sa->length < sb->length) ? sa->length : sb->length;

        const char* ca = string_chars(sa);
        const char* cb = string_chars(sb);

        int cmp = memcmp(ca, cb, min_len);
        if (cmp != 0) return (cmp > 0) ? 1 : -1;
        if (sa->length < sb->length) return -1;
        if (sa->length > sb->length) return 1;
        return 0;
    }

    return 0;
}

// 文字列結合 (OP_CONCAT 用)
Value calc_concat(GCHeap* heap, Value a, Value b) 
{
    ObjString* sa = as_string(a);
    ObjString* sb = as_string(b);
    int length = sa->length + sb->length;
    char* chars = (char*)malloc(length + 1);
    memcpy(chars, string_chars(sa), sa->length);
    memcpy(chars + sa->length, string_chars(sb), sb->length);
    chars[length] = '\0';
    ObjString* res = gc_new_string(heap, chars, length);
    free(chars);
    return make_obj((Obj*)res);
}
