#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "symbol.h"
#include "table.h"
#include "class.h"
#include "literal.h"

static void test_symbols(void)
{
    printf("[1] Symbol Interning Test...\n");

    ObjString* a1 = intern_cstr("hello");
    ObjString* a2 = intern_cstr("hello");
    ObjString* b  = intern_cstr("world");

    // 同一文字列ならアドレスが完全一致することを確認
    assert(a1 == a2);
    // 異なる文字列なら別アドレスになることを確認
    assert(a1 != b);
    assert(a1->hash == a2->hash);

    printf("  -> OK: Symbol pointer equality passed.\n");
}

static void test_table(void)
{
    printf("[2] Hash Table Test (Pointer Comparison)...\n");

    Table* table = new_table(4);
    ObjString* key_x = intern_cstr("x");
    ObjString* key_y = intern_cstr("y");

    table_set(table, key_x, make_int(100));
    table_set(table, key_y, make_int(200));

    Value out;
    // 登録した値の取得検証
    assert(table_get(table, key_x, &out) == true);
    assert(is_int(out) && as_int(out) == 100);

    assert(table_get(table, key_y, &out) == true);
    assert(is_int(out) && as_int(out) == 200);

    // 上書き検証
    table_set(table, key_x, make_int(999));
    assert(table_get(table, key_x, &out) == true);
    assert(as_int(out) == 999);

    // 削除検証
    table_delete(table, key_x);
    assert(table_get(table, key_x, &out) == false);
    assert(table_get(table, key_y, &out) == true);

    table_free(table);
    printf("  -> OK: Table set/get/override/delete passed.\n");
}

static void test_class_and_delegation(void)
{
    printf("[3] Class Builder, Delegation & Method Lookup Test...\n");

    // 1. 委譲元クラス（Delegate）を作成
    ObjClass* delegate_cls = new_class(intern_cstr("Delegate"), NULL, NULL, NULL, 0);
    ObjString* m_delegated = intern_cstr("delegated_action");
    add_method(delegate_cls, m_delegated, make_int(777)); // ダミーのメソッド値

    // 2. メインクラス（MainClass）を作成 (委譲先枠: 1)
    ObjClass* main_cls = new_class(intern_cstr("MainClass"), NULL, NULL, NULL, 1);
    set_delegate(main_cls, 0, delegate_cls, NULL);

    // 自身のメソッドを登録
    ObjString* m_own = intern_cstr("own_action");
    add_method(main_cls, m_own, make_int(123));

    // フィールドの追加
    ObjString* f_name = intern_cstr("count");
    FieldInfo f_info = new_field_info(f_name, NULL, NULL, false);
    add_field(main_cls, f_info, make_int(42));

    // 3. メソッド探索検証
    Value found;

    // (A) 自身のメソッドが見つかるか
    assert(find_method(main_cls, m_own, &found) == true);
    assert(is_int(found) && as_int(found) == 123);

    // (B) 委譲先のメソッドが見つかるか (from探索)
    assert(find_method(main_cls, m_delegated, &found) == true);
    assert(is_int(found) && as_int(found) == 777);

    // (C) 存在しないメソッドが弾かれるか
    ObjString* m_unknown = intern_cstr("unknown");
    assert(find_method(main_cls, m_unknown, &found) == false);

    // 4. インスタンス生成と初期値検証
    printf("[4] Instance Instantiation Test...\n");
    ObjInstance* inst = new_instance(main_cls);
    assert(inst->klass == main_cls);
    assert(is_int(inst->fields[0]) && as_int(inst->fields[0]) == 42);

    // クラス構造のダンプ表示を追加
    printf("\n--- Class Inspection ---\n");
    print_class(main_cls);
    printf("------------------------\n\n");

    // インスタンスのフィールド書き換え
    inst->fields[0] = make_int(84);
    assert(as_int(inst->fields[0]) == 84);

    // クラス構造のダンプ表示を追加
    printf("\n--- Class Inspection ---\n");
    print_class(main_cls);
    printf("------------------------\n\n");

    // クリーンアップ
    free(inst->fields);
    free(inst);
    free_class(main_cls);
    free_class(delegate_cls);

    printf("  -> OK: Method dispatch & Instance fields passed.\n");
}

int main(void)
{
    printf("=== JapEng Runtime Unit Test ===\n");

    test_symbols();
    test_table();
    test_class_and_delegation();

    free_symbol_pool();

    printf("\n>>> All runtime tests passed successfully! <<<\n");
    return 0;
}
