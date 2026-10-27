#include "class.h"
#include "table.h" 
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if DEBUG_MODE
// 型メタデータ（ジェネリクス対応）の再帰表示
static void print_type_info(const TypeInfo* type)
{
    if (type == NULL) {
        fprintf(stderr, "Any");
        return;
    }
    fprintf(stderr, "%s", type->name ? type->name->chars : "Unknown");
    if (type->type_arg_count > 0) {
        for (int i = 0; i < type->type_arg_count; i++) {
            fprintf(stderr, " ");
            print_type_info(type->type_args[i]);
        }
    }
}

// 初期値（NaN Boxing Value）の簡易フォーマッタ
static void print_value_repr(Value value)
{
    if (is_nil(value)) {
        fprintf(stderr, "nil");
    } else if (is_bool(value)) {
        fprintf(stderr, "%s", as_bool(value) ? "true" : "false");
    } else if (is_int(value)) {
        fprintf(stderr, "%lld", (long long)as_int(value));
    } else if (is_float(value)) {
        fprintf(stderr, "%g", as_float(value));
    } else if (is_obj(value)) {
        fprintf(stderr, "<obj @%p>", as_obj(value));
    } else {
        fprintf(stderr, "<value 0x%016llx>", (unsigned long long)value);
    }
}

void print_class(ObjClass* klass)
{
    if (klass == NULL) {
        fprintf(stderr, "<class (null)>\n");
        return;
    }

    // 1. クラス宣言ヘッダ
    fprintf(stderr, "<class %s", klass->name ? klass->name->chars : "Anonymous");
    if (klass->type != NULL && klass->type->type_arg_count > 0) {
        for (int i = 0; i < klass->type->type_arg_count; i++) {
            fprintf(stderr, " ");
            print_type_info(klass->type->type_args[i]);
        }
    }
    fprintf(stderr, ">");

    // 継承元
    if (klass->superclass != NULL || klass->superclass_type != NULL) {
        fprintf(stderr, " [based ");
        if (klass->superclass_type != NULL) {
            print_type_info(klass->superclass_type);
        } else if (klass->superclass != NULL && klass->superclass->name != NULL) {
            fprintf(stderr, "%s", klass->superclass->name->chars);
        }
        fprintf(stderr, "]");
    }

    // 委譲元リスト
    if (klass->delegate_count > 0) {
        fprintf(stderr, " [from ");
        for (int i = 0; i < klass->delegate_count; i++) {
            if (i > 0) fprintf(stderr, ", ");
            if (klass->delegate_types != NULL && klass->delegate_types[i] != NULL) {
                print_type_info(klass->delegate_types[i]);
            } else if (klass->delegates != NULL && klass->delegates[i] != NULL && klass->delegates[i]->name != NULL) {
                fprintf(stderr, "%s", klass->delegates[i]->name->chars);
            } else {
                fprintf(stderr, "Unknown");
            }
        }
        fprintf(stderr, "]");
    }
    fprintf(stderr, "\n");

    // 2. フィールド情報
    fprintf(stderr, "fields:\n");
    if (klass->field_count == 0) {
        fprintf(stderr, "  (none)\n");
    } else {
        for (int i = 0; i < klass->field_count; i++) {
            FieldInfo* f = &klass->field_infos[i];
            fprintf(stderr, "  - ");
            if (f->is_static) fprintf(stderr, "static ");
            fprintf(stderr, "%s: ", f->name ? f->name->chars : "(unnamed)");
            print_type_info(f->type);

            // デフォルト初期値
            if (klass->default_values != NULL) {
                fprintf(stderr, " = ");
                print_value_repr(klass->default_values[i]);
            }

            // 委譲元の明示
            if (f->from_class != NULL) {
                fprintf(stderr, " (from %s)", f->from_class->name ? f->from_class->name->chars : "Unknown");
            }
            fprintf(stderr, "\n");
        }
    }

    // 3. メソッド一覧（Table 内の OCCUPIED スロットを走査）
    fprintf(stderr, "methods:\n");
    bool has_methods = false;
    if (klass->methods != NULL && klass->methods->entries != NULL) {
        for (int i = 0; i < klass->methods->capacity; i++) {
            HashEntry* entry = &klass->methods->entries[i];
            if (entry->status == OCCUPIED && entry->key != NULL) {
                fprintf(stderr, "  - %s", entry->key->chars);

                // ★ 委譲元の明示
                ObjClass* from_cls = get_method_from(klass, entry->key);
                if (from_cls != NULL && from_cls->name != NULL) {
                    fprintf(stderr, " (from %s)", from_cls->name->chars);
                }
                fprintf(stderr, "\n");
                has_methods = true;
            }
        }
    }
    if (!has_methods) {
        fprintf(stderr, "  (none)\n");
    }

    // 4. vtable レイアウト表示
    fprintf(stderr, "vtable (%d slots):\n", klass->vtable_count);
    for (int i = 0; i < klass->vtable_count; i++) {
        fprintf(stderr, "  [%d] %s\n", i, klass->vtable_names[i] ? klass->vtable_names[i]->chars : "(anon)");
    }
}

void print_instance(ObjInstance* inst)
{
    if (inst == NULL) {
        fprintf(stderr, "<instance (null)>\n");
        return;
    }

    fprintf(stderr, "<instance of %s @%p>\n", 
           inst->klass && inst->klass->name ? inst->klass->name->chars : "Unknown", 
           (void*)inst);
    fprintf(stderr, "instance fields:\n");

    if (inst->klass == NULL || inst->klass->field_count == 0) {
        fprintf(stderr, "  (none)\n");
    } else {
        for (int i = 0; i < inst->klass->field_count; i++) {
            FieldInfo* f = &inst->klass->field_infos[i];
            fprintf(stderr, "  - %s = ", f->name ? f->name->chars : "(unnamed)");
            print_value_repr(inst->fields[i]);
            fprintf(stderr, "\n");
        }
    }
}

#endif
TypeInfo* new_type_info(ObjString* name, int type_arg_count) {
    TypeInfo* t = (TypeInfo*)malloc(sizeof(TypeInfo) + sizeof(TypeInfo*) * type_arg_count);
    t->name = name;
    t->type_arg_count = type_arg_count;
    t->param_count = 0;
    t->min_param_count = 0;
    for (int i = 0; i < 16; i++) t->default_values[i] = make_nil();
    for (int i = 0; i < type_arg_count; i++) t->type_args[i] = NULL;
    return t;
}

TypeInfo* new_func_type_info(int param_count, TypeInfo** params, int ret_count, TypeInfo** rets) 
{
    int total_types = param_count + ret_count;
    TypeInfo* t = (TypeInfo*)malloc(sizeof(TypeInfo) + sizeof(TypeInfo*) * total_types);
    t->name = sym_Function;
    t->type_arg_count = total_types;
    t->param_count = param_count;
    t->min_param_count = param_count;
    for (int i = 0; i < 16; i++) t->default_values[i] = make_nil();

    // 引数型をセット
    for (int i = 0; i < param_count; i++) {
        t->type_args[i] = params[i];
    }
    // 戻り値型をセット (0個なら Void)
    if (ret_count > 0) {
        for (int i = 0; i < ret_count; i++) {
            t->type_args[param_count + i] = rets[i];
        }
    } else {
        t->type_args[param_count] = new_type_info(sym_Void, 0);
    }
    return t;
}

FieldInfo new_field_info(ObjString* name, TypeInfo* type,
                          ObjClass* from_class, bool is_static)
{
    FieldInfo f = {
        .name = name,
        .type = type,
        .from_class = from_class,
        .is_static = is_static
    };
    return f;
}

ObjClass* new_class(ObjString* name, TypeInfo* type, 
                    ObjClass* superclass, TypeInfo* superclass_type,
                    int delegate_count)
{
    ObjClass* klass = (ObjClass*)malloc(sizeof(ObjClass));
    klass->header.type = OBJ_CLASS;
    klass->name = name;
    klass-> type = type;
    klass->superclass = superclass;
    klass->superclass_type = superclass_type;

    klass->delegate_count = delegate_count;
    if (delegate_count > 0) {
        klass->delegates = (ObjClass**)malloc(sizeof(ObjClass*) * delegate_count);
        klass->delegate_types = (TypeInfo**)malloc(sizeof(TypeInfo*) * delegate_count);
        for (int index = 0; index < delegate_count; index++) {
            klass->delegates[index] = NULL;
            klass->delegate_types[index] = NULL;
        }
    } else {
        klass->delegates = NULL;
        klass->delegate_types = NULL;
    }

    klass->methods = new_table(8);
    klass->method_from = new_table(8);
    klass->method_types = new_table(8);

    klass->vtable_count = 0;
    klass->vtable_capacity = 0;
    klass->vtable = NULL;
    klass->vtable_names = NULL;
    klass->vtable_indexes = new_table(8);

    klass->field_count = 0;
    klass->field_capacity = 0;
    klass->field_infos = NULL;
    klass->default_values = NULL;

    if (superclass != NULL && superclass->field_count > 0) {
        for (int i = 0; i < superclass->field_count; i++) {
            FieldInfo f = superclass->field_infos[i];
            // 由来元が空の場合は、直接の親クラスを記録
            if (f.from_class == NULL) {
                f.from_class = superclass;
            }
            add_field(klass, f, superclass->default_values[i]);
        }
    }

    klass->state = CLASS_STATE_UNCOMPILED;

    return klass;
}

void free_class(ObjClass* klass)
{
    if (klass == NULL) return;

    if (klass->delegates != NULL) {
        free(klass->delegates);
        free(klass->delegate_types);
    }
    if (klass->field_infos != NULL) {
        free(klass->field_infos);
        free(klass->default_values);
    }
    if (klass->methods != NULL) {
        table_free(klass->methods);
    }
    if (klass->method_types != NULL) {
        table_free(klass->method_types);
    }

    if (klass->vtable != NULL) {
        free(klass->vtable);
        free(klass->vtable_names);
    }
    if (klass->vtable_indexes != NULL) {
        table_free(klass->vtable_indexes);
    }

    free(klass);
}

void set_delegate(ObjClass* klass, int index,
                  ObjClass* delegate_class, TypeInfo* delegate_type)
{
    if (index >= 0 && index < klass->delegate_count) {
        klass->delegates[index] = delegate_class;
        klass->delegate_types[index] = delegate_type;
    }
}

void add_field(ObjClass* klass, FieldInfo field_info, Value default_value)
{
    if (klass->field_count + 1 > klass->field_capacity) {
        klass->field_capacity = klass->field_capacity < 8 ? 8 : klass->field_capacity * 2;
        klass->field_infos = (FieldInfo*)realloc(
            klass->field_infos, sizeof(FieldInfo) * klass->field_capacity
        );
        klass->default_values = (Value*)realloc(
            klass->default_values, sizeof(Value) * klass->field_capacity
        );
    }

    int idx = klass->field_count++;
    klass->field_infos[idx] = field_info;
    klass->default_values[idx] = default_value;
}

void add_method(ObjClass* klass, ObjString* name, Value method)
{
    table_set(klass->methods, name, method);
}

void add_method_from(ObjClass* klass, ObjString* name, ObjClass* from_class)
{
    if (klass->method_from == NULL) klass->method_from = new_table(8);
    table_set(klass->method_from, name, make_obj((Obj*)from_class));
}

bool find_method(ObjClass* klass, ObjString* name, Value* out_method)
{
    if (table_get(klass->methods, name, out_method)) {
        return true;
    }

    for (int index = 0; index < klass->delegate_count; index++) {
        if (klass->delegates[index] != NULL) {
            if (find_method(klass->delegates[index], name, out_method)) {
                return true;
            }
        }
    }

    if (klass->superclass != NULL) {
        return find_method(klass->superclass, name, out_method);
    }

    return false;
}

ObjClass* get_method_from(ObjClass* klass, ObjString* name)
{
    if (klass == NULL || klass->method_from == NULL) return NULL;
    Value val;
    if (table_get(klass->method_from, name, &val)) {
        return (ObjClass*)as_obj(val);
    }
    return NULL;
}

ObjInstance* new_instance(ObjClass* klass)
{
    ObjInstance* instance = (ObjInstance*)malloc(sizeof(ObjInstance));
    instance->header.type = OBJ_INSTANCE;
    instance->klass = klass;

    if (klass->field_count > 0) {
        instance->fields = (Value*)malloc(sizeof(Value) * klass->field_count);

        if (klass->default_values != NULL) {
            memcpy(instance->fields, klass->default_values, sizeof(Value) * klass->field_count);
        } else {
            for (int index = 0; index < klass->field_count; index++) {
                instance->fields[index] = make_nil();
            }
        }
        for (int index = 0; index < klass->field_count; index++) {
            if (klass->field_infos[index].is_delegate_instance && klass->field_infos[index].from_class != NULL) {
                ObjInstance* del_inst = new_instance(klass->field_infos[index].from_class);
                instance->fields[index] = make_obj((Obj*)del_inst);
            }
        }
    } else {
        instance->fields = NULL;
    }

    return instance;
}

int find_field_index(ObjClass* klass, ObjString* name)
{
    for (int index = 0; index < klass->field_count; index++) {
        if (klass->field_infos[index].name == name) {
            return index;
        }
    }
    return -1;
}

void add_method_type(ObjClass* klass, ObjString* name, TypeInfo* type) 
{
    if (klass->method_types == NULL) klass->method_types = new_table(8);
    table_set(klass->method_types, name, make_obj((Obj*)type));
}

TypeInfo* find_method_type(ObjClass* klass, ObjString* name) 
{
    if (klass == NULL || klass->method_types == NULL) return NULL;
    Value val;
    if (table_get(klass->method_types, name, &val)) {
        return (TypeInfo*)as_obj(val);
    }

    if (klass->superclass != NULL) {
        return find_method_type(klass->superclass, name);
    }

    for (int i = 0; i < klass->delegate_count; i++) {
        TypeInfo* t = find_method_type(klass->delegates[i], name);
        if (t != NULL) return t;
    }
    return NULL;
}

ObjString* get_delegate_field_name(ObjClass* delegate_class)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "__delegate_%s", 
             (delegate_class && delegate_class->name) ? delegate_class->name->chars : "anon");
    return intern_cstr(buf);
}

int add_or_override_vtable_slot(ObjClass* klass, ObjString* name, Value method)
{
    Value existing_slot_val;
    // 1. 親クラスから引き継いだ既存スロットがある場合（オーバーライド）
    if (table_get(klass->vtable_indexes, name, &existing_slot_val)) {
        int slot = as_int(existing_slot_val);
        // 実体がある場合（body != NULL やネイティブ）のみ中身を上書き
        if (!is_nil(method)) {
            klass->vtable[slot] = method;
        }
        return slot;
    }

    // 2. 新規スロットの追加（容量拡張）
    if (klass->vtable_count >= klass->vtable_capacity) {
        klass->vtable_capacity = klass->vtable_capacity < 8 ? 8 : klass->vtable_capacity * 2;
        klass->vtable = (Value*)realloc(klass->vtable, sizeof(Value) * klass->vtable_capacity);
        klass->vtable_names = (ObjString**)realloc(klass->vtable_names, sizeof(ObjString*) * klass->vtable_capacity);
    }

    int slot = klass->vtable_count++;
    klass->vtable[slot] = method; // 実体がなければ make_nil() で枠だけ確保
    klass->vtable_names[slot] = name;
    table_set(klass->vtable_indexes, name, make_int(slot));
    return slot;
}

// クラスのコンパイル完了時に呼び出し、確定した vtable を組み立てる
void build_class_vtable(ObjClass* klass)
{
    if (klass == NULL) return;

    // A. 親クラスがある場合、親の vtable を丸ごとコピーして引き継ぐ
    if (klass->superclass != NULL) {
        ObjClass* super = klass->superclass;
        if (super->vtable_count > 0) {
            klass->vtable_capacity = super->vtable_count < 8 ? 8 : super->vtable_count;
            klass->vtable = (Value*)malloc(sizeof(Value) * klass->vtable_capacity);
            klass->vtable_names = (ObjString**)malloc(sizeof(ObjString*) * klass->vtable_capacity);
            klass->vtable_count = super->vtable_count;

            for (int i = 0; i < super->vtable_count; i++) {
                klass->vtable[i] = super->vtable[i];
                klass->vtable_names[i] = super->vtable_names[i];
                table_set(klass->vtable_indexes, super->vtable_names[i], make_int(i));
            }
        }
    }

    // B. 固定スロット 0 (init) と 1 (destroy) を確実に登録/上書き
    Value init_val, destroy_val;
    if (find_method(klass, sym_init, &init_val)) {
        add_or_override_vtable_slot(klass, sym_init, init_val);
    }
    if (find_method(klass, sym_destroy, &destroy_val)) {
        add_or_override_vtable_slot(klass, sym_destroy, destroy_val);
    }
}

void init_class_vtable_from_super(ObjClass* klass)
{
    if (klass == NULL) return;

    // 親クラスの vtable をそのままコピー
    if (klass->superclass != NULL) {
        ObjClass* super = klass->superclass;
        if (super->vtable_count > 0) {
            klass->vtable_capacity = super->vtable_count < 8 ? 8 : super->vtable_count;
            klass->vtable = (Value*)malloc(sizeof(Value) * klass->vtable_capacity);
            klass->vtable_names = (ObjString**)malloc(sizeof(ObjString*) * klass->vtable_capacity);
            klass->vtable_count = super->vtable_count;

            for (int i = 0; i < super->vtable_count; i++) {
                klass->vtable[i] = super->vtable[i];
                klass->vtable_names[i] = super->vtable_names[i];
                table_set(klass->vtable_indexes, super->vtable_names[i], make_int(i));
            }
        }
    }

    // スロット 0 (init) と 1 (destroy) を予約確保（なければ枠だけ作成）
    if (get_vtable_index(klass, sym_init) == -1) {
        add_or_override_vtable_slot(klass, sym_init, make_nil());
    }
    if (get_vtable_index(klass, sym_destroy) == -1) {
        add_or_override_vtable_slot(klass, sym_destroy, make_nil());
    }
}

int get_vtable_index(ObjClass* klass, ObjString* name)
{
    if (klass == NULL || klass->vtable_indexes == NULL) return -1;
    Value slot_val;
    if (table_get(klass->vtable_indexes, name, &slot_val)) {
        return as_int(slot_val);
    }
    return -1;
}
