#include <stdio.h>
#include <string.h>
#include "builtins.h"
#include "vm.h"
#include "vm_class.h"
#include "native.h"
#include "symbol.h"
#include "table.h"

// void define_native(ObjClass* klass, const char* name, NativeFn func) 
// {
//     Symbol* sym = intern_cstr(name);
//     ObjNative* native = new_native(func, sym);
//     table_set(klass->vtable_names, sym, make_obj((Obj*)native));

//     add_or_override_vtable_slot(klass, sym, make_obj((Obj*)native));
// }

// 何もしない空の Object.init
static bool native_object_init(VM* vm, uint8_t arg_count) {
    // レシーバ (self) はそのままスタックトップに残す
    return true;
}

// 何もしない空の Object.destroy
static bool native_object_destroy(VM* vm, uint8_t arg_count) {
    pop(vm); // self を pop
    return true;
}

// 組み込みクラスのセットアップ
void init_builtin_classes(VM* vm) 
{
    vm->class_object = vm_new_class(sym_Object, NULL);
    vm_define_native(vm->class_object, "init",    native_object_init);
    vm_define_native(vm->class_object, "destroy", native_object_destroy);
    setup_print(vm, vm->class_object);

    table_set(vm->globals, sym_Object, make_obj((Obj*)vm->class_object));

    setup_number(vm);
    setup_boolean(vm);
    setup_list(vm);
    setup_string(vm);
}
