#ifndef SYMBOL_H_
#define SYMBOL_H_

typedef struct objString ObjString;

ObjString* intern_string(const char* chars, int length);

ObjString* intern_cstr(const char* str);

void free_symbol_pool(void);

#endif
