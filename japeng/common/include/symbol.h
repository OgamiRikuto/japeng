#ifndef SYMBOL_H_
#define SYMBOL_H_
#include "object.h"

typedef struct symbol {
    Obj      header; // 移行期間中は make_obj 互換のため残す (type: OBJ_STRING)
    uint32_t hash;
    int      length;
    char     chars[];
} Symbol;

// typedef Symbol ObjString;

// typedef struct objString ObjString;

#define CORE_SYMBOLS(X) \
    X(is,           "is") \
    X(are,          "are") \
    X(ellipsis,     "...") \
    X(self,         "self") \
    X(super,        "super") \
    X(if,           "if") \
    X(elif,         "elif") \
    X(else,         "else") \
    X(repeat,       "repeat") \
    X(switch,       "switch") \
    X(case,         "case") \
    X(default,      "default") \
    X(each,         "each") \
    X(init,         "init") \
    X(destroy,      "destroy") \
    X(new,          "new") \
    X(call,         "call") \
    X(Auto,         "Auto") \
    X(SmallInteger, "SmallInteger") \
    X(Function,     "Function") \
    X(and,          "and") \
    X(or,           "or") \
    X(not,          "!") \
    X(plus,         "+") \
    X(minus,        "-") \
    X(multi,        "*") \
    X(div,          "/") \
    X(rem,          "%") \
    X(equal,        "=") \
    X(nequal,       "!=") \
    X(less,         "<") \
    X(nless,        ">=") \
    X(gt,           ">") \
    X(ngt,          "<=") \
    X(plus_eq,      "+=") \
    X(minus_eq,     "-=") \
    X(multi_eq,     "*=") \
    X(div_eq,       "/=") \
    X(shl,          "<<") \
    X(shr,          ">>") \
    X(band,         "&") \
    X(bor,          "|") \
    X(bxor,         "^") \
    X(bnot,         "~") \
    X(shl_eq,       "<<=") \
    X(shr_eq,       ">>=") \
    X(band_eq,      "&=") \
    X(bor_eq,       "|=") \
    X(bxor_eq,      "^=") \
    X(Object,       "Object") \
    X(Integer,      "Integer") \
    X(BigInteger,   "BigInteger") \
    X(Float,        "Float") \
    X(Decimal,      "Decimal") \
    X(String,       "String") \
    X(List,         "List") \
    X(Boolean,      "Boolean") \
    X(Null,         "Null") \
    X(__multi__,    "__multi__") \
    X(Void,         "Void") \
    X(Error,        "Error")

#define DECLARE_SYM(name, str) extern Symbol* sym_##name;
CORE_SYMBOLS(DECLARE_SYM)
#undef DECLARE_SYM

void init_symbols(void);
Symbol* intern_string(const char* chars, int length);

Symbol* intern_cstr(const char* str);

void free_symbol_pool(void);

#endif
