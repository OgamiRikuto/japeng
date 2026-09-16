#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "symbol.h"
#include "table.h"
#include "class.h"
#include "literal.h"

// -------------------------------------------------------------
// パターン 1: All 0（最小・空クラス）
// ジェネリクス: 0, based: 0, from: 0, field: 0, method: 0
// -------------------------------------------------------------
static void test_pattern_empty(void)
{
    printf("\n=== [Pattern 1: All Zero (Minimal Empty Class)] ===\n");

    ObjString* name = intern_cstr("Empty");
    ObjClass* cls = new_class(name, NULL, NULL, NULL, 0);

    // 設計図の表示
    print_class(cls);

    // インスタンス化 (field 0個でも安全に動くか)
    ObjInstance* inst = new_instance(cls);
    assert(inst->fields == NULL);
    print_instance(inst);

    // 存在しないメソッド探索でクラッシュしないか
    Value dummy;
    assert(find_method(cls, intern_cstr("foo"), &dummy) == false);

    free(inst);
    free_class(cls);
    printf("-> Pattern 1 Passed.\n");
}

// -------------------------------------------------------------
// パターン 2: All 1（単数構成クラス）
// ジェネリクス: 1, based: 1 (型引数なし), from: 1, field: 1, method: 1
// -------------------------------------------------------------
static void test_pattern_singular(void)
{
    printf("\n=== [Pattern 2: Singular (1 Generic, 1 based, 1 from, 1 Field, 1 Method)] ===\n");

    ObjString* sym_int = intern_cstr("Integer");
    TypeInfo* type_int = new_type_info(sym_int, 0);

    // 1. 継承元 (based Parent)
    ObjClass* cls_parent = new_class(intern_cstr("Parent"), NULL, NULL, NULL, 0);

    // 2. 委譲元 (from DelegateA)
    ObjClass* cls_del_a = new_class(intern_cstr("DelegateA"), NULL, NULL, NULL, 0);

    // 3. メインクラス: Box T [based Parent] [from DelegateA]
    TypeInfo* type_box = new_type_info(intern_cstr("Box"), 1);
    type_box->type_args[0] = new_type_info(intern_cstr("T"), 0);

    ObjClass* cls_box = new_class(intern_cstr("Box"), type_box, cls_parent, NULL, 1);
    set_delegate(cls_box, 0, cls_del_a, NULL);

    // フィールド: 1個 (初期値あり)
    FieldInfo f_val = new_field_info(intern_cstr("value"), type_int, NULL, false);
    add_field(cls_box, f_val, make_int(100));

    // メソッド: 1個
    add_method(cls_box, intern_cstr("get_value"), make_int(1));

    print_class(cls_box);

    // インスタンス化 & 書き換え
    ObjInstance* inst = new_instance(cls_box);
    print_instance(inst);
    assert(as_int(inst->fields[0]) == 100);

    inst->fields[0] = make_int(200);
    assert(as_int(inst->fields[0]) == 200);

    // クリーンアップ
    free(inst->fields);
    free(inst);
    free_class(cls_box);
    free_class(cls_parent);
    free_class(cls_del_a);
    printf("-> Pattern 2 Passed.\n");
}

// -------------------------------------------------------------
// パターン 3: All 2 / Complex（複数構成・全種別フィールド・ディスパッチ優先順位）
// ジェネリクス: 2 (K, V)
// based: 1 (型引数1: BaseHolder Integer)
// from: 2 (Del1 String, Del2)
// fields: 全パターン (インスタンス値あり, デフォルトnil, 静的, 委譲元由来2種)
// methods: 衝突時の優先順位検証 (自前 > 委譲1 > 委譲2 > 親)
// -------------------------------------------------------------
static void test_pattern_multiple_and_dispatch(void)
{
    printf("\n=== [Pattern 3: Multiple (Generics 2, from 2, Full Fields, Dispatch Order)] ===\n");

    ObjString* sym_int = intern_cstr("Integer");
    TypeInfo* type_int = new_type_info(sym_int, 0);

    ObjString* sym_str = intern_cstr("String");
    TypeInfo* type_str = new_type_info(sym_str, 0);

    // 共通で被るメソッド名
    ObjString* m_shadowed  = intern_cstr("conflict_all"); // 全員が持つ
    ObjString* m_del_vs_del= intern_cstr("del_conflict");  // Del1 と Del2 が持つ
    ObjString* m_super_only= intern_cstr("super_action");  // Parent のみ

    // 1. 親クラス: BaseHolder Integer
    TypeInfo* type_parent = new_type_info(intern_cstr("BaseHolder"), 1);
    type_parent->type_args[0] = type_int;
    ObjClass* cls_super = new_class(intern_cstr("BaseHolder"), type_parent, NULL, NULL, 0);
    add_method(cls_super, m_shadowed, make_int(400));    // 最低優先度
    add_method(cls_super, m_super_only, make_int(999));

    // 2. 委譲元1: Del1 String
    TypeInfo* type_del1 = new_type_info(intern_cstr("Del1"), 1);
    type_del1->type_args[0] = type_str;
    ObjClass* cls_del1 = new_class(intern_cstr("Del1"), type_del1, NULL, NULL, 0);
    add_method(cls_del1, m_shadowed, make_int(200));
    add_method(cls_del1, m_del_vs_del, make_int(201)); // Del2 より先に勝つべき

    // 3. 委譲元2: Del2
    TypeInfo* type_del2 = new_type_info(intern_cstr("Del2"), 0);
    ObjClass* cls_del2 = new_class(intern_cstr("Del2"), type_del2, NULL, NULL, 0);
    add_method(cls_del2, m_shadowed, make_int(300));
    add_method(cls_del2, m_del_vs_del, make_int(301)); // 負けるべき

    // 4. メインクラス: Map K V [based BaseHolder Integer] [from Del1 String, Del2]
    TypeInfo* type_map = new_type_info(intern_cstr("Map"), 2);
    type_map->type_args[0] = new_type_info(intern_cstr("K"), 0);
    type_map->type_args[1] = new_type_info(intern_cstr("V"), 0);

    ObjClass* cls_map = new_class(intern_cstr("Map"), type_map, cls_super, type_parent, 2);
    set_delegate(cls_map, 0, cls_del1, type_del1);
    set_delegate(cls_map, 1, cls_del2, type_del2);

    // 自身のメソッド登録 (全員に勝つ)
    add_method(cls_map, m_shadowed, make_int(100));

    // 5. フィールドの全バリエーション追加
    // (A) 自前インスタンスフィールド (デフォルト初期値あり)
    add_field(cls_map, new_field_info(intern_cstr("size"), type_int, NULL, false), make_int(0));

    // (B) 自前インスタンスフィールド (初期値なし = nil)
    add_field(cls_map, new_field_info(intern_cstr("tag"), type_str, NULL, false), make_nil());

    // (C) 静的フィールド
    add_field(cls_map, new_field_info(intern_cstr("capacity_limit"), type_int, NULL, true), make_int(1024));

    // (D) 委譲元 Del1 由来フィールド
    add_field(cls_map, new_field_info(intern_cstr("del1_prop"), type_str, cls_del1, false), make_nil());

    // (E) 委譲元 Del2 由来フィールド
    add_field(cls_map, new_field_info(intern_cstr("del2_prop"), type_int, cls_del2, false), make_int(50));

    // クラスのダンプ確認
    print_class(cls_map);

    // インスタンスの初期化ダンプ確認
    ObjInstance* inst = new_instance(cls_map);
    print_instance(inst);

    // 初期値のアサーション
    assert(as_int(inst->fields[0]) == 0);
    assert(is_nil(inst->fields[1]));
    assert(as_int(inst->fields[2]) == 1024);
    assert(is_nil(inst->fields[3]));
    assert(as_int(inst->fields[4]) == 50);

    // インスタンスの複数フィールド書き換え
    inst->fields[0] = make_int(5);
    inst->fields[1] = make_bool(true); // nil だったスロットに代入
    inst->fields[4] = make_int(77);

    printf("\n--- Instance After Multiple Field Modifications ---\n");
    print_instance(inst);
    assert(as_int(inst->fields[0]) == 5);
    assert(as_bool(inst->fields[1]) == true);
    assert(as_int(inst->fields[4]) == 77);

    // 6. メソッドディスパッチ優先順位の厳密検証
    Value res;

    // 検証 1: 自身 vs 全体 -> 自身 (100) が勝つ
    assert(find_method(cls_map, m_shadowed, &res) && as_int(res) == 100);

    // 検証 2: 委譲1 vs 委譲2 -> 先に登録された Del1 (201) が勝つ
    assert(find_method(cls_map, m_del_vs_del, &res) && as_int(res) == 201);

    // 検証 3: 親クラスのみ -> 親 (999) が解決される
    assert(find_method(cls_map, m_super_only, &res) && as_int(res) == 999);

    // クリーンアップ
    free(inst->fields);
    free(inst);
    free_class(cls_map);
    free_class(cls_super);
    free_class(cls_del1);
    free_class(cls_del2);
    printf("-> Pattern 3 Passed.\n");
}

int main(void)
{
    printf("##################################################\n");
    printf("###  JapEng Class Combinatorial Exhaustive Test ###\n");
    printf("##################################################\n");

    test_pattern_empty();
    test_pattern_singular();
    test_pattern_multiple_and_dispatch();

    free_symbol_pool();

    printf("\n>>> [SUCCESS] All combinatorial states and dispatch priorities verified! <<<\n");
    return 0;
}
