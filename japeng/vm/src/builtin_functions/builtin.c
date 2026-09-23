#include <stdio.h>
#include <string.h>
#include "builtins.h"
#include "vm.h"
#include "class.h"
#include "native.h"
#include "symbol.h"
#include "table.h"

static void define_native(ObjClass* klass, const char* name, NativeFn func) 
{
    ObjString* sym = intern_cstr(name);
    ObjNative* native = new_native(func, sym);
    table_set(klass->methods, sym, make_obj((Obj*)native));
}

// 組み込みクラスのセットアップ
void init_builtin_classes(VM* vm) 
{
    // 1. 基底 Object クラス
    ObjString* sym_Object = intern_cstr("Object");
    TypeInfo* o_info = new_type_info(sym_Object, 0);
    vm->class_object = new_class(sym_Object, o_info, NULL, NULL, 0);
    define_native(vm->class_object, "print", native_print);
    define_native(vm->class_object, "println", native_println);

    // 2. Integer クラス (Object を継承)
    ObjString* sym_Integer = intern_cstr("Integer");
    vm->class_integer = new_class(sym_Integer, new_type_info(sym_Integer, 0), vm->class_object, o_info, 0);
}
