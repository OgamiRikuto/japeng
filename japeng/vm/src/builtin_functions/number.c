#include "common.h"
#include "vm.h"
#include "builtins.h"

#include <stdbool.h>
#include <stdint.h>

static bool native_int_pow(VM* vm, uint8_t arg_count)
{
    if (arg_count != 1) {
        error_runtime(ERR_WRONG_ARG_COUNT, 1, arg_count);
        return false;
    }
    Value exp_val = pop(vm);
    Value rec_val = pop(vm);

    bool rec_is_num = is_int(rec_val) || is_obj_type(rec_val, OBJ_BIGINTEGER);
    bool exp_is_num = is_int(exp_val) || is_obj_type(exp_val, OBJ_BIGINTEGER);

    if (!rec_is_num || !exp_is_num) {
        error_runtime(ERR_NON_NUMERIC_OPERATION, "pow", "Non-Integer");
        return false;
    }

    // 1. 指数の取得 (負数なら 0 を返す)
    uint64_t exp = 0;
    if (is_int(exp_val)) {
        int32_t iexp = as_int(exp_val);
        if (iexp < 0) {
            push(vm, make_int(0));
            return true;
        }
        exp = (uint64_t)iexp;
    } else {
        BigInteger* b_exp = (BigInteger*)as_obj(exp_val);
        if (!b_exp->sign) {
            push(vm, make_int(0));
            return true;
        }
        if (b_exp->length > 2) {
            error_runtime(ERR_FLOAT_OVERFLOW, "Exponent too large");
            return false;
        }
        exp = b_exp->nums[0];
        if (b_exp->length == 2) {
            exp |= ((uint64_t)b_exp->nums[1] << 32);
        }
    }

    // 2. 底の BigInteger 化 (SmallInt なら一時オブジェクト化)
    BigInteger* base_bi = is_int(rec_val) 
                          ? bi_from_i64(as_int(rec_val)) 
                          : (BigInteger*)as_obj(rec_val);

    // 3. べき乗計算
    BigInteger* res_bi = bi_pow(base_bi, exp);

    if (is_int(rec_val)) {
        free_bi(base_bi);
    }

    // 4. 32bit に収まれば SmallInteger に自動降格してプッシュ
    push(vm, bi_to_value_and_demote(res_bi));
    return true;
}

// ★ 追加: gcd メソッド (バイナリGCD呼び出し)
static bool native_int_gcd(VM* vm, uint8_t arg_count)
{
    if (arg_count != 1) {
        error_runtime(ERR_WRONG_ARG_COUNT, 1, arg_count);
        return false;
    }
    Value b_val = pop(vm);
    Value a_val = pop(vm);

    bool a_is_num = is_int(a_val) || is_obj_type(a_val, OBJ_BIGINTEGER);
    bool b_is_num = is_int(b_val) || is_obj_type(b_val, OBJ_BIGINTEGER);

    if (!a_is_num || !b_is_num) {
        error_runtime(ERR_NON_NUMERIC_OPERATION, "gcd", "Non-Integer");
        return false;
    }

    BigInteger* ba = is_int(a_val) ? bi_from_i64(as_int(a_val)) : (BigInteger*)as_obj(a_val);
    BigInteger* bb = is_int(b_val) ? bi_from_i64(as_int(b_val)) : (BigInteger*)as_obj(b_val);

    BigInteger* res = bi_gcd(ba, bb);

    if (is_int(a_val)) free_bi(ba);
    if (is_int(b_val)) free_bi(bb);

    push(vm, bi_to_value_and_demote(res));
    return true;
}

void setup_number(VM* vm, TypeInfo* o_info) 
{
    // Integer クラス
    vm->class_integer = new_class(sym_Integer, new_type_info(sym_Integer, 0), vm->class_object, o_info, 0);
    define_native(vm->class_integer, "pow", native_int_pow);
    define_native(vm->class_integer, "gcd", native_int_gcd); // ★ 追加！

    // Float クラス
    vm->class_float = new_class(sym_Float, new_type_info(sym_Float, 0), vm->class_object, o_info, 0);

    // Decimal クラス (★ 第3引数はスーパークラスなので vm->class_object を指定！)
    vm->class_decimal = new_class(sym_Decimal, new_type_info(sym_Decimal, 0), vm->class_object, o_info, 0);
}
