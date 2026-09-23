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
    // 基底 Object クラス
    ObjString* sym_Object = intern_cstr("Object");
    TypeInfo* o_info = new_type_info(sym_Object, 0);
    vm->class_object = new_class(sym_Object, o_info, NULL, NULL, 0);
    define_native(vm->class_object, "print", native_print);
    define_native(vm->class_object, "println", native_println);

    // Integer クラス (Object を継承)
    ObjString* sym_Integer = intern_cstr("Integer");
    vm->class_integer = new_class(sym_Integer, new_type_info(sym_Integer, 0), vm->class_object, o_info, 0);

    // List クラス
    ObjString* sym_List = intern_cstr("List");
    TypeInfo* l_info = new_type_info(sym_List, 1);
    vm->class_list = new_class(sym_List, l_info, vm->class_object, o_info, 0);

    define_native(vm->class_list, "init",   native_list_init);
    define_native(vm->class_list, "push",   native_list_push);
    define_native(vm->class_list, "get",    native_list_get);
    define_native(vm->class_list, "set",    native_list_set);
    define_native(vm->class_list, "length", native_list_length);

    table_set(vm->globals, sym_List, make_obj((Obj*)vm->class_list));
}
