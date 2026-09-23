#ifndef OBJECT_H
#define OBJECT_H

#include <stdint.h>
#include <stdbool.h>
#include "literal.h"

typedef struct chunk Chunk;

// オブジェクトの種類タグ
typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_CLOSURE,
    OBJ_LIST,
    OBJ_CLASS,
    OBJ_INSTANCE,
    OBJ_NATIVE,
    OBJ_MAX
} ObjType;

// オブジェクトの共通ヘッダー
typedef struct obj {
    ObjType type;
} Obj;

// 文字列オブジェクト(シンボル)
typedef struct objString {
    Obj header;
    uint32_t hash;
    int length;
    char chars[];
} ObjString;

typedef struct upvalueInfo {
    uint8_t index;
    bool is_local;
} UpvalueInfo;

// 関数オブジェクト
typedef struct objFunc {
    Obj header;
    Chunk* chunk;
    uint8_t arity;
    int upvalue_count;
    UpvalueInfo* upvalues;
} ObjFunction;

typedef struct objClosure {
    Obj header;
    ObjFunction* function;
    int capture_count;
    Value captures[];
} ObjClosure;

static inline ObjType obj_type(Value value) {
    return ((Obj*)as_obj(value))->type;
}

static inline bool is_obj_type(Value value, ObjType type) {
    return is_obj(value) && (obj_type(value) == type);
}

static inline bool is_function(Value value) {
    return is_obj_type(value, OBJ_FUNCTION);
}

static inline bool is_closure(Value value) {
    return is_obj_type(value, OBJ_CLOSURE);
}

ObjFunction* new_function(Chunk* chunk, uint8_t arity);
ObjClosure* new_closure(ObjFunction* function);

#endif
