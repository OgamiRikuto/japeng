#ifndef VM_CALC_H
#define VM_CALC_H

#include <stdint.h>
#include <stdbool.h>
#include "common.h"

#define as_decimal(v) ((ObjDecimal*)as_obj(v))
#define as_string(v)  ((ObjString*)as_obj(v))

// --- 最適化整数オペコード用 (OP_ADD_INT 等) ---
// 32bit 整数同士を計算し、溢れたら BigInteger の Value を返す
Value calc_add_int(int32_t a, int32_t b);
Value calc_sub_int(int32_t a, int32_t b);
Value calc_mul_int(int32_t a, int32_t b);
Value calc_div_int(int32_t a, int32_t b);
Value calc_rem_int(int32_t a, int32_t b);
Value calc_add_int_slow(Value a, Value b);
Value calc_sub_int_slow(Value a, Value b);
Value calc_mul_int_slow(Value a, Value b);
Value calc_div_int_slow(Value a, Value b);
Value calc_rem_int_slow(Value a, Value b);
int calc_compare_int_slow(Value a, Value b);

// --- 比較演算用 (<=> 三値比較: a < b なら -1, a == b なら 0, a > b なら 1) ---
int calc_compare(Value a, Value b);
Value calc_concat(Value a, Value b);

#endif
