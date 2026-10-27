#include <stdio.h>
#include <string.h>
#include "builtins.h"
#include "vm.h"
#include "class.h"
#include "native.h"
#include "symbol.h"
#include "table.h"

void define_native(ObjClass* klass, const char* name, NativeFn func) 
{
    ObjString* sym = intern_cstr(name);
    ObjNative* native = new_native(func, sym);
    table_set(klass->methods, sym, make_obj((Obj*)native));

    add_or_override_vtable_slot(klass, sym, make_obj((Obj*)native));
}

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
    // 基底 Object クラス
    TypeInfo* o_info = new_type_info(sym_Object, 0);
    vm->class_object = new_class(sym_Object, o_info, NULL, NULL, 0);
    define_native(vm->class_object, "init", native_object_init);
    define_native(vm->class_object, "destroy", native_object_destroy);
    setup_print(vm, vm->class_object);

    // Integer, Float, Decimal クラス
    setup_number(vm, o_info);

    // Boolean クラス
    setup_boolean(vm, o_info);

    // List クラス
    setup_list(vm, o_info);

    // String クラス
    setup_string(vm, o_info);

    print_class(vm->class_object);
    print_class(vm->class_integer);
    print_class(vm->class_float);
    print_class(vm->class_decimal);
    print_class(vm->class_boolean);
    print_class(vm->class_list);
    print_class(vm->class_string);
}
