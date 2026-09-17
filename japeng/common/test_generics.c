#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "symbol.h"
#include "table.h"
#include "class.h"
#include "literal.h"

// 型パラメータを具体型へ再帰置換するヘルパー
static TypeInfo* resolve_type(TypeInfo* target, ObjString** params, TypeInfo** args, int count)
{
    if (target == NULL) return NULL;

    // 型パラメータ名と一致したら具体型へ置換
    for (int i = 0; i < count; i++) {
        if (target->name == params[i] && target->type_arg_count == 0) {
            return args[i];
        }
    }

    // ネストされた型引数も再帰的に解決
    TypeInfo* resolved = new_type_info(target->name, target->type_arg_count);
    for (int i = 0; i < target->type_arg_count; i++) {
        resolved->type_args[i] = resolve_type(target->type_args[i], params, args, count);
    }
    return resolved;
}

// 汎用クラスから具体化されたクラス（Specialized Class）を生成
static ObjClass* specialize_class(ObjClass* generic_cls, TypeInfo** concrete_args, int arg_count)
{
    // クラス全体の型定義 (例: List Integer や Map String Integer)
    TypeInfo* spec_type = new_type_info(generic_cls->name, arg_count);
    for (int i = 0; i < arg_count; i++) {
        spec_type->type_args[i] = concrete_args[i];
    }

    ObjClass* spec_cls = new_class(generic_cls->name, spec_type, 
                                   generic_cls->superclass, generic_cls->superclass_type, 
                                   generic_cls->delegate_count);

    // 型パラメータ一覧を親の型定義から抽出
    ObjString* params[8];
    for (int i = 0; i < generic_cls->type->type_arg_count; i++) {
        params[i] = generic_cls->type->type_args[i]->name;
    }

    // 各フィールドの型を置換して追加
    for (int i = 0; i < generic_cls->field_count; i++) {
        FieldInfo orig = generic_cls->field_infos[i];
        TypeInfo* resolved_type = resolve_type(orig.type, params, concrete_args, arg_count);
        FieldInfo new_f = new_field_info(orig.name, resolved_type, orig.from_class, orig.is_static);
        add_field(spec_cls, new_f, generic_cls->default_values[i]);
    }

    return spec_cls;
}

// --- テスト本体 ---
int main(void)
{
    printf("=== JapEng Generics Verification Test ===\n\n");

    // 基本型のシンボル準備
    ObjString* sym_int = intern_cstr("Integer");
    ObjString* sym_str = intern_cstr("String");
    TypeInfo* type_int = new_type_info(sym_int, 0);
    TypeInfo* type_str = new_type_info(sym_str, 0);

    // =========================================================
    // テスト 1: List Type -> List Integer
    // =========================================================
    printf("[1] Testing: List Type -> List Integer\n");

    // 1-1. 汎用クラス定義: class List Type (element: Type)
    ObjString* sym_list = intern_cstr("List");
    ObjString* sym_type_param = intern_cstr("Type");
    TypeInfo* type_list_generic = new_type_info(sym_list, 1);
    type_list_generic->type_args[0] = new_type_info(sym_type_param, 0);

    ObjClass* cls_list_generic = new_class(sym_list, type_list_generic, NULL, NULL, 0);
    add_field(cls_list_generic, new_field_info(intern_cstr("element"), type_list_generic->type_args[0], NULL, false), make_nil());

    // 1-2. 具体化: arr: List Integer
    TypeInfo* list_args[1] = { type_int };
    ObjClass* cls_list_int = specialize_class(cls_list_generic, list_args, 1);

    print_class(cls_list_int);

    // 1-3. インスタンス生成と値の代入 (x: Integer new 10)
    ObjInstance* arr = new_instance(cls_list_int);
    arr->fields[0] = make_int(10); // x を代入

    print_instance(arr);

    // 1-4. アサーション検証
    // (A) クラスメタデータの型が "Integer" に置き換わっているか
    assert(cls_list_int->field_infos[0].type->name == sym_int);
    // (B) 実体の値が正真正銘の Integer 型 (is_int) か
    assert(is_int(arr->fields[0]));
    assert(as_int(arr->fields[0]) == 10);

    printf("  -> OK: List element is strictly Integer.\n\n");

    // =========================================================
    // テスト 2: Map Key, Value -> Map String, Integer
    // =========================================================
    printf("[2] Testing: Map Key, Value -> Map String, Integer\n");

    // 2-1. 汎用クラス定義: class Map Key, Value (k: Key, v: Value)
    ObjString* sym_map = intern_cstr("Map");
    ObjString* sym_key_param = intern_cstr("Key");
    ObjString* sym_val_param = intern_cstr("Value");
    TypeInfo* type_map_generic = new_type_info(sym_map, 2);
    type_map_generic->type_args[0] = new_type_info(sym_key_param, 0);
    type_map_generic->type_args[1] = new_type_info(sym_val_param, 0);

    ObjClass* cls_map_generic = new_class(sym_map, type_map_generic, NULL, NULL, 0);
    add_field(cls_map_generic, new_field_info(intern_cstr("k"), type_map_generic->type_args[0], NULL, false), make_nil());
    add_field(cls_map_generic, new_field_info(intern_cstr("v"), type_map_generic->type_args[1], NULL, false), make_nil());

    // 2-2. 具体化: dict: Map String, Integer
    TypeInfo* map_args[2] = { type_str, type_int };
    ObjClass* cls_map_str_int = specialize_class(cls_map_generic, map_args, 2);

    print_class(cls_map_str_int);

    // 2-3. インスタンス生成と値の代入 (key: String, value: Integer)
    ObjInstance* dict = new_instance(cls_map_str_int);
    ObjString* sample_str = intern_cstr("my_key");
    dict->fields[0] = make_obj(sample_str); // key 代入
    dict->fields[1] = make_int(42);         // value 代入

    print_instance(dict);

    // 2-4. アサーション検証
    // (A) メタデータ型の置換確認
    assert(cls_map_str_int->field_infos[0].type->name == sym_str);
    assert(cls_map_str_int->field_infos[1].type->name == sym_int);
    // (B) 実体メモリの値型確認
    assert(is_obj(dict->fields[0]) && (ObjString*)as_obj(dict->fields[0]) == sample_str);
    assert(is_int(dict->fields[1]) && as_int(dict->fields[1]) == 42);

    printf("  -> OK: Map (k: String, v: Integer) verified perfectly.\n\n");

    // クリーンアップ
    free(arr->fields);
    free(arr);
    free_class(cls_list_int);
    free_class(cls_list_generic);

    free(dict->fields);
    free(dict);
    free_class(cls_map_str_int);
    free_class(cls_map_generic);

    free_symbol_pool();

    printf(">>> All Generics tests passed successfully! <<<\n");
    return 0;
}
