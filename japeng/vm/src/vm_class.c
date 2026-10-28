#include "vm_class.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ObjClass* vm_new_class(Symbol* name, ObjClass* superclass) 
{
    ObjClass* klass = (ObjClass*)malloc(sizeof(ObjClass));
    klass->header.type = OBJ_CLASS;
    klass->name = name;
    klass->superclass = superclass;

    // 1. 親クラスのフィールドを継承
    if (superclass != NULL && superclass->field_count > 0) {
        klass->field_count    = superclass->field_count;
        klass->default_values = (Value*)malloc(sizeof(Value) * klass->field_count);
        klass->field_names    = (Symbol**)malloc(sizeof(Symbol*) * klass->field_count);

        for (uint32_t i = 0; i < superclass->field_count; i++) {
            klass->default_values[i] = superclass->default_values[i];
            klass->field_names[i]    = superclass->field_names[i];
        }
    } else {
        klass->field_count    = 0;
        klass->default_values = NULL;
        klass->field_names    = NULL;
    }

    // 2. 親クラスの vtable を継承
    if (superclass != NULL && superclass->vtable_count > 0) {
        klass->vtable_count    = superclass->vtable_count;
        klass->vtable_capacity = superclass->vtable_count;
        klass->vtable          = (Value*)malloc(sizeof(Value) * klass->vtable_capacity);
        klass->vtable_names    = (Symbol**)malloc(sizeof(Symbol*) * klass->vtable_capacity);

        for (uint16_t i = 0; i < superclass->vtable_count; i++) {
            klass->vtable[i]       = superclass->vtable[i];
            klass->vtable_names[i] = superclass->vtable_names[i];
        }
    } else {
        klass->vtable_count    = 0;
        klass->vtable_capacity = 0;
        klass->vtable          = NULL;
        klass->vtable_names    = NULL;
    }

    return klass;
}

uint32_t vm_define_field(ObjClass* klass, const char* name, Value default_value) 
{
    Symbol* sym = intern_cstr(name);
    uint32_t slot = klass->field_count++;

    klass->default_values = (Value*)realloc(klass->default_values, sizeof(Value) * klass->field_count);
    klass->field_names    = (Symbol**)realloc(klass->field_names, sizeof(Symbol*) * klass->field_count);

    klass->field_names[slot]    = sym;
    klass->default_values[slot] = default_value;

    return slot;
}

void vm_define_native(ObjClass* klass, const char* name, NativeFn func) 
{
    Symbol* sym = intern_cstr(name);

    // 既存スロットのオーバーライド
    for (uint16_t i = 0; i < klass->vtable_count; i++) {
        if (klass->vtable_names[i] == sym) {
            klass->vtable[i] = make_obj((Obj*)new_native(func, sym));
            return;
        }
    }

    // 新規スロットの追加（倍々拡張）
    if (klass->vtable_count >= klass->vtable_capacity) {
        klass->vtable_capacity = (klass->vtable_capacity < 8) ? 8 : klass->vtable_capacity * 2;
        klass->vtable       = (Value*)realloc(klass->vtable, sizeof(Value) * klass->vtable_capacity);
        klass->vtable_names = (Symbol**)realloc(klass->vtable_names, sizeof(Symbol*) * klass->vtable_capacity);
    }

    uint16_t slot = klass->vtable_count++;
    klass->vtable_names[slot] = sym;
    klass->vtable[slot]       = make_obj((Obj*)new_native(func, sym));
}

void vm_free_class(ObjClass* klass) 
{
    if (klass == NULL) return;
    if (klass->default_values != NULL) free(klass->default_values);
    if (klass->field_names != NULL)    free(klass->field_names);
    if (klass->vtable != NULL)         free(klass->vtable);
    if (klass->vtable_names != NULL)   free(klass->vtable_names);
    free(klass);
}
