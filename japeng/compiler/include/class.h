#ifndef CLASS_H
#define CLASS_H

#define DEBUG_MODE 1

#include "object.h"
#include <stdbool.h>

typedef struct table Table;

// 型データ (名前, 型, 型数)
typedef struct typeInfo {
    Symbol* name;
    int type_arg_count;
    int param_count;
    int min_param_count;
    Value default_values[16];
    struct typeInfo* type_args[];
} TypeInfo;

// フィールドデータ (名前, 型, 委譲元, 静的か)
typedef struct fieldInfo {
    Symbol* name;
    TypeInfo* type;
    struct classDef* from_class;
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
typedef struct classDef {
    Obj header;
    
    // クラスの情報
    Symbol* name;
    TypeInfo* type;

    // 継承元の情報
    struct classDef* superclass;
    TypeInfo* superclass_type;

    // 委譲元の情報
    struct classDef** delegates;
    TypeInfo** delegate_types;
    int delegate_count;

    // メソッド
    Table* methods;
    Table* method_from;
    Table* method_types;

    // vtable（仮想関数テーブル)
    int vtable_count;
    int vtable_capacity;
    Value* vtable;               // メソッド実体 (ObjFunction* / ObjNative*) の配列
    Symbol** vtable_names;    // 各スロットのメソッド名配列 (デバッグ・継承用)
    Table* vtable_indexes;       // メソッド名 -> スロット番号 (int) の逆引きマップ
    
    // フィールド情報
    int field_count; 
    int field_capacity;
    FieldInfo* field_infos;
    Value* default_values;

    ClassState state;
} ClassDef;

// typedef struct objIns {
//     Obj header;
//     ClassDef* klass;
//     Value* fields;
// } ObjInstance;

#if DEBUG_MODE
void print_class(ClassDef* klass);
// void print_instance(ObjInstance* inst);
#endif

TypeInfo* new_type_info(Symbol* name, int type_arg_count);

TypeInfo* new_func_type_info(int param_count, TypeInfo** params, int ret_count, TypeInfo** rets);
FieldInfo new_field_info(Symbol* name, TypeInfo* type,
                          ClassDef* from_class, bool is_static);
ClassDef* new_class(Symbol* name, TypeInfo* type, 
                    ClassDef* superclass, TypeInfo* superclass_type,
                    int delegate_count);
void free_class(ClassDef* klass);

void set_delegate(ClassDef* klass, int index,
                  ClassDef* delegate_class, TypeInfo* delegate_type);

void add_field(ClassDef* klass, FieldInfo field_info, Value default_value);

void add_method(ClassDef* klass, Symbol* name, Value method);
void add_method_from(ClassDef* klass, Symbol* name, ClassDef* from_class);
bool find_method(ClassDef* klass, Symbol* name, Value* out_method);
ClassDef* get_method_from(ClassDef* klass, Symbol* name);

// ObjInstance* new_instance(ClassDef* klass);

int find_field_index(ClassDef* klass, Symbol* name);

void add_method_type(ClassDef* klass, Symbol* name, TypeInfo* type);
TypeInfo* find_method_type(ClassDef* klass, Symbol* name);

Symbol* get_delegate_field_name(ClassDef* delegate_class);

int add_or_override_vtable_slot(ClassDef* klass, Symbol* name, Value method);
void build_class_vtable(ClassDef* klass);
void init_class_vtable_from_super(ClassDef* klass);
int get_vtable_index(ClassDef* klass, Symbol* name);


#endif
