#ifndef SYMBOL_H_
#define SYMBOL_H_

typedef struct objString ObjString;

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
    X(init,         "init") \
    X(new,          "new") \
    X(call,         "call") \
    X(Auto,         "Auto") \
    X(SmallInteger, "SmallInteger") \
    X(Function,     "Function") \
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
    X(lshif,        "<<") \
    X(rshif,        ">>") \
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

#define DECLARE_SYM(name, str) extern ObjString* sym_##name;
CORE_SYMBOLS(DECLARE_SYM)
#undef DECLARE_SYM

void init_symbols(void);
ObjString* intern_string(const char* chars, int length);

ObjString* intern_cstr(const char* str);

void free_symbol_pool(void);

#endif
