#include "builtins.h"
#include "vm_class.h"
#include "symbol.h"
#include <stdio.h>

void set_static_int(ObjClass* klass, const char* name, int32_t val) 
{
    Symbol* sym = intern_cstr(name);
    for (uint16_t i = 0; i < klass->static_field_count; i++) {
        if (klass->static_field_names[i] == sym) {
            klass->static_fields[i] = make_int(val);
            return;
        }
    }
}

void vm_define_static_native(ObjClass* klass, const char* name, NativeFn func) 
{
    if (klass == NULL || name == NULL) return;
    Symbol* sym = intern_cstr(name);

    for (uint16_t i = 0; i < klass->static_method_count; i++) {
        if (klass->static_method_names[i] == sym) {
            klass->static_methods[i] = make_obj((Obj*)new_native(func, sym));
            return;
        }
    }
}

// クラス名を見て、ネイティブバインドが必要なら関数を差し込む
void bind_native_class_if_needed(ObjClass* klass) 
{
    if (klass == NULL || klass->name == NULL) return;

    if (klass->name == sym_File) {
        register_file_natives(klass);
        return;
    }

    if (klass->name == sym_Clock) {
        register_clock_natives(klass);
        return;
    }

    if (klass->name == sym_Random) {
        register_random_natives(klass);
        return;
    }

    if (klass->name == sym_Window) {
        register_window_natives(klass);
        return;
    }

    if (klass->name == sym_Input) {
        register_input_natives(klass);
        return;
    }

    if (klass->name == sym_Key) {
        register_key_natives(klass);
        return;
    }


}
