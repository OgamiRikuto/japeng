#include "class.h"

#include <stdlib.h>
#include <string.h>

TypeInfo* new_type_info(const char* name, int type_arg_count)
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

FieldInfo new_field_info(const char* name, TypeInfo* type,
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

ObjClass* new_class(const char* name, TypeInfo* type, 
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

    klass->methods = NULL;

    klass->field_count = 0;
    klass->field_capacity = 0;
    klass->field_infos = NULL;
    klass->default_values = NULL;

    return klass;
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
