#include "common.h"
#include "ast.h"
#include <math.h>

Value parse_ast_literal(ASTNode* node, TypeInfo** out_type) 
{
    LiteralKind kind = node->literal.kind;
    ObjString* raw   = node->literal.raw;
    const char* str  = raw->chars;
    int len          = raw->length;

    switch (kind) {
        case TOKEN_INTEGER: {
            size_t start = (str[0] == '+' || str[0] == '-') ? 1 : 0;
            size_t num_digits = len - start;

            // 9桁以下: 絶対に 32bit 符号付き整数に収まる最速パス
            if (num_digits <= 9) {
                int32_t val = (int32_t)strtol(str, NULL, 10);
                if (out_type) *out_type = new_type_info(sym_Integer, 0);
                return make_int(val);
            }

            // 10〜18桁: 64bit 整数で試す
            if (num_digits <= 18) {
                char* end;
                int64_t val = strtoll(str, &end, 10);
                if (*end == '\0') {
                    if (val >= INT32_MIN && val <= INT32_MAX) {
                        if (out_type) *out_type = new_type_info(sym_Integer, 0);
                        return make_int((int32_t)val);
                    }
                    // 32bit を超えたら BigInteger へ昇格
                    BigInteger* bi = bi_from_i64(val);
                    if (out_type) *out_type = new_type_info(sym_Integer, 0);
                    return make_obj((Obj*)bi);
                }
            }

            // 19桁以上: BigInteger パース
            BigInteger* bi = new_bi_from_string(raw);
            if (out_type) *out_type = new_type_info(sym_Integer, 0);
            return make_obj((Obj*)bi);
        }

        case TOKEN_FLOAT: {
            double val = strtod(str, NULL);
            if (out_type) *out_type = new_type_info(sym_Float, 0);
            return make_float(val);
        }

        case TOKEN_STRING: {
            if (out_type) *out_type = new_type_info(sym_String, 0);
            return make_obj((Obj*)raw);
        }

        

        default:
            if (out_type) *out_type = new_type_info(sym_Error, 0);
            return make_nil();
    }
}
