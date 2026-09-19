#include "class.h"
#include "table.h" 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if DEBUG_MODE
// 型メタデータ（ジェネリクス対応）の再帰表示
static void print_type_info(const TypeInfo* type)
{
    if (type == NULL) {
        printf("Any");
        return;
    }
    printf("%s", type->name ? type->name->chars : "Unknown");
    if (type->type_arg_count > 0 && type->type_args != NULL) {
        for (int i = 0; i < type->type_arg_count; i++) {
            printf(" ");
            print_type_info(type->type_args[i]);
        }
    }
}

// 初期値（NaN Boxing Value）の簡易フォーマッタ
static void print_value_repr(Value value)
{
    if (is_nil(value)) {
        printf("nil");
    } else if (is_bool(value)) {
        printf("%s", as_bool(value) ? "true" : "false");
    } else if (is_int(value)) {
        printf("%lld", (long long)as_int(value));
    } else if (is_float(value)) {
        printf("%g", as_float(value));
    } else if (is_obj(value)) {
        printf("<obj @%p>", as_obj(value));
    } else {
        printf("<value 0x%016llx>", (unsigned long long)value);
    }
}

void print_class(ObjClass* klass)
{
    if (klass == NULL) {
        printf("<class (null)>\n");
        return;
    }

    // 1. クラス宣言ヘッダ
    printf("<class %s", klass->name ? klass->name->chars : "Anonymous");
    if (klass->type != NULL && klass->type->type_arg_count > 0) {
        for (int i = 0; i < klass->type->type_arg_count; i++) {
            printf(" ");
            print_type_info(klass->type->type_args[i]);
        }
    }
    printf(">");

    // 継承元
    if (klass->superclass != NULL || klass->superclass_type != NULL) {
        printf(" [based ");
        if (klass->superclass_type != NULL) {
            print_type_info(klass->superclass_type);
        } else if (klass->superclass != NULL && klass->superclass->name != NULL) {
            printf("%s", klass->superclass->name->chars);
        }
        printf("]");
    }

    // 委譲元リスト
    if (klass->delegate_count > 0) {
        printf(" [from ");
        for (int i = 0; i < klass->delegate_count; i++) {
            if (i > 0) printf(", ");
            if (klass->delegate_types != NULL && klass->delegate_types[i] != NULL) {
                print_type_info(klass->delegate_types[i]);
            } else if (klass->delegates != NULL && klass->delegates[i] != NULL && klass->delegates[i]->name != NULL) {
                printf("%s", klass->delegates[i]->name->chars);
            } else {
                printf("Unknown");
            }
        }
        printf("]");
    }
    printf("\n");

    // 2. フィールド情報
    printf("fields:\n");
    if (klass->field_count == 0) {
        printf("  (none)\n");
    } else {
        for (int i = 0; i < klass->field_count; i++) {
            FieldInfo* f = &klass->field_infos[i];
            printf("  - ");
            if (f->is_static) printf("static ");
            printf("%s: ", f->name ? f->name->chars : "(unnamed)");
            print_type_info(f->type);

            // デフォルト初期値
            if (klass->default_values != NULL) {
                printf(" = ");
                print_value_repr(klass->default_values[i]);
            }

            // 委譲元の明示
            if (f->from_class != NULL) {
                printf(" (from %s)", f->from_class->name ? f->from_class->name->chars : "Unknown");
            }
            printf("\n");
        }
    }

    // 3. メソッド一覧（Table 内の OCCUPIED スロットを走査）
    printf("methods:\n");
    bool has_methods = false;
    if (klass->methods != NULL && klass->methods->entries != NULL) {
        for (int i = 0; i < klass->methods->capacity; i++) {
            HashEntry* entry = &klass->methods->entries[i];
            if (entry->status == OCCUPIED && entry->key != NULL) {
                printf("  - %s", entry->key->chars);

                // ★ 委譲元の明示
                ObjClass* from_cls = get_method_from(klass, entry->key);
                if (from_cls != NULL && from_cls->name != NULL) {
                    printf(" (from %s)", from_cls->name->chars);
                }
                printf("\n");
                has_methods = true;
            }
        }
    }
    if (!has_methods) {
        printf("  (none)\n");
    }
}

void print_instance(ObjInstance* inst)
{
    if (inst == NULL) {
        printf("<instance (null)>\n");
        return;
    }

    printf("<instance of %s @%p>\n", 
           inst->klass && inst->klass->name ? inst->klass->name->chars : "Unknown", 
           (void*)inst);
    printf("instance fields:\n");

    if (inst->klass == NULL || inst->klass->field_count == 0) {
        printf("  (none)\n");
    } else {
        for (int i = 0; i < inst->klass->field_count; i++) {
            FieldInfo* f = &inst->klass->field_infos[i];
            printf("  - %s = ", f->name ? f->name->chars : "(unnamed)");
            print_value_repr(inst->fields[i]);
            printf("\n");
        }
    }
}

#endif
TypeInfo* new_type_info(ObjString* name, int type_arg_count)
{
    TypeInfo* t = (TypeInfo*)malloc(sizeof(TypeInfo));
    t->name = name;
    t->type_arg_count = type_arg_count;
    if (type_arg_count > 0) {
        t->type_args = (TypeInfo**)malloc(sizeof(TypeInfo*) * type_arg_count);
        for (int index = 0; index < type_arg_count; index++) {
            t->type_args[index] = NULL;
        }
    } else {
        t->type_args = NULL;
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
