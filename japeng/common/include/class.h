#ifndef CLASS_H
#define CLASS_H

#define DEBUG_MODE 1

#include "object.h"
#include <stdbool.h>

typedef struct table Table;

// 型データ (名前, 型, 型数)
typedef struct typeInfo {
    ObjString* name;
    int type_arg_count;
    int param_count;
    int min_param_count;
    Value default_values[16];
    struct typeInfo* type_args[];
} TypeInfo;

// フィールドデータ (名前, 型, 委譲元, 静的か)
typedef struct fieldInfo {
    ObjString* name;
    TypeInfo* type;
    struct objClass* from_class;
    bool is_static;
    bool is_delegate_instance;
} FieldInfo;

typedef enum {
    CLASS_STATE_UNCOMPILED = 0,
    CLASS_STATE_COMPILING,
    CLASS_STATE_COMPILED,
    CLASS_STATE_MAX
} ClassState;

// クラスデータ
typedef struct objClass {
    Obj header;
    
    // クラスの情報
    ObjString* name;
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
    Table* method_from;
    Table* method_types;;
    
    // フィールド情報
    int field_count; 
    int field_capacity;
    FieldInfo* field_infos;
    Value* default_values;

    ClassState state;
} ObjClass;

typedef struct objIns {
    Obj header;
    ObjClass* klass;
    Value* fields;
} ObjInstance;

#if DEBUG_MODE
void print_class(ObjClass* klass);
void print_instance(ObjInstance* inst);
#endif

TypeInfo* new_type_info(ObjString* name, int type_arg_count);

TypeInfo* new_func_type_info(int param_count, TypeInfo** params, int ret_count, TypeInfo** rets);
FieldInfo new_field_info(ObjString* name, TypeInfo* type,
                          ObjClass* from_class, bool is_static);
ObjClass* new_class(ObjString* name, TypeInfo* type, 
                    ObjClass* superclass, TypeInfo* superclass_type,
                    int delegate_count);
void free_class(ObjClass* klass);

void set_delegate(ObjClass* klass, int index,
                  ObjClass* delegate_class, TypeInfo* delegate_type);

void add_field(ObjClass* klass, FieldInfo field_info, Value default_value);

void add_method(ObjClass* klass, ObjString* name, Value method);
void add_method_from(ObjClass* klass, ObjString* name, ObjClass* from_class);
bool find_method(ObjClass* klass, ObjString* name, Value* out_method);
ObjClass* get_method_from(ObjClass* klass, ObjString* name);

ObjInstance* new_instance(ObjClass* klass);

int find_field_index(ObjClass* klass, ObjString* name);

void add_method_type(ObjClass* klass, ObjString* name, TypeInfo* type);
TypeInfo* find_method_type(ObjClass* klass, ObjString* name);

ObjString* get_delegate_field_name(ObjClass* delegate_class);


#endif
