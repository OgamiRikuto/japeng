#ifndef CLASS_H_
#define CLASS_H

#include "object.h"
#include <stdbool.h>

typedef struct table Table;

// 型データ (名前, 型, 型数)
typedef struct typeInfo {
    const char* name;
    struct typeInfo** type_args;
    int type_arg_count;
} TypeInfo;

// フィールドデータ (名前, 型, 委譲元, 静的か)
typedef struct fieldInfo {
    const char* name;
    TypeInfo* type;
    struct objClass* from_class;
    bool is_static;
} FieldInfo;

// クラスデータ
typedef struct objClass {
    Obj header;
    
    // クラスの情報
    const char* name;
    TypeInfo* type;

    // 継承元の情報
    struct objClass* superclass;
    TypeInfo* superclass_type;

    // 委譲元の情報
    struct objClass** delegates;
    TypeInfo** delegate_types;
    int delegate_count;

    // メソッド
    Table* methods;
    
    // フィールド情報
    int field_count; 
    int field_capacity;
    FieldInfo* field_infos;
    Value* default_values;
} ObjClass;

typedef struct objIns {
    Obj header;
    ObjClass* klass;
    Value* fields;
} ObjInstance;

ObjClass* new_class(const char* name, TypeInfo* type, 
                    ObjClass* superclass, TypeInfo* superclass_type,
                    int delegate_count);

void set_delegate(ObjClass* klass, int index,
                  ObjClass* delegate_class, TypeInfo* delegate_type);

void add_field(ObjClass* klass, FieldInfo* field_info, Value default_value);

ObjInstance* new_instance(ObjClass* klass);

TypeInfo* new_type_info(const char* name, int type_arg_count);
FieldInfo new_field_info(const char* name, TypeInfo* type,
                          ObjClass* from_class, bool is_static);

#endif
