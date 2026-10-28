#ifndef VM_CLASS_H
#define VM_CLASS_H

#include <stdint.h>
#include <stdbool.h>
#include "literal.h"
#include "symbol.h"
#include "object.h"
#include "native.h"

typedef struct ObjClass {
    Obj              header;
    Symbol*          name;
    struct ObjClass* superclass;
    
    // フィールド情報
    uint32_t         field_count;
    Value*           default_values;
    Symbol**         field_names;

    // メソッドディスパッチ用
    uint16_t         vtable_count;
    uint16_t         vtable_capacity;
    Value*           vtable;
    Symbol**         vtable_names;
} ObjClass;

// クラス生成 (数値指定なし)
ObjClass* vm_new_class(Symbol* name, ObjClass* superclass);

// フィールド・ネイティブメソッド登録 API
uint32_t  vm_define_field(ObjClass* klass, const char* name, Value default_value);
void      vm_define_native(ObjClass* klass, const char* name, NativeFn func);

void      vm_free_class(ObjClass* klass);

#endif // VM_CLASS_H
