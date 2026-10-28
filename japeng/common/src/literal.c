#include "common.h"
#include "ast.h"
#include <math.h>

Value parse_ast_literal(ASTNode* node) 
{
    LiteralKind kind = node->literal.kind;
    Symbol* raw   = node->literal.raw;
    const char* str  = raw->chars;
    int len          = raw->length;

    switch (kind) {
        case TOKEN_INTEGER: {
            size_t start = (str[0] == '+' || str[0] == '-') ? 1 : 0;
            size_t num_digits = len - start;

            if (num_digits <= 9) {
                return make_int((int32_t)strtol(str, NULL, 10));
            }
            if (num_digits <= 18) {
                char* end;
                int64_t val = strtoll(str, &end, 10);
                if (*end == '\0') {
                    if (val >= INT32_MIN && val <= INT32_MAX) {
                        return make_int((int32_t)val);
                    }
                    return make_obj((Obj*)bi_from_i64(val));
                }
            }
            return make_obj((Obj*)new_bi_from_string(raw));
        }
        case TOKEN_FLOAT:
            return make_float(strtod(str, NULL));
        case TOKEN_STRING:
            return make_obj((Obj*)raw);
        case TOKEN_TRUE:
            return make_bool(true);
        case TOKEN_FALSE:
            return make_bool(false);
        case TOKEN_NULL:
        default:
            return make_nil();
    }
}
