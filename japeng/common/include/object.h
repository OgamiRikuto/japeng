#ifndef OBJECT_H
#define OBJECT_H

#include <stdint.h>
#include <stdbool.h>
#include "literal.h"

typedef struct chunk Chunk;
typedef struct symbol Symbol;
// typedef struct symbol ObjString;

// オブジェクトの種類タグ
typedef enum {
    OBJ_BIGINTEGER,
    OBJ_DECIMAL,
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
    uint8_t  type;       // ObjType (1バイトで読み出すため、隣のflagsと合体しない！)
    uint8_t  flags;      // GCフラグ (0x80 など)
    uint16_t unused;     // アライメント用予約
    uint32_t slot_idx;   // スロット番号
} Obj;

#include "symbol.h"
// // 文字列オブジェクト(シンボル)
// typedef struct objString {
//     Obj header;
//     uint32_t hash;
//     int length;
//     char chars[];
// } ObjString;

// // リストオブジェクト
// typedef struct objList {
//     Obj header;
//     Value* elements;
//     int size;
//     int capacity;
// } ObjList;

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

// typedef struct objClosure {
//     Obj header;
//     ObjFunction* function;
//     int capture_count;
//     Value captures[];
// } ObjClosure;

static inline ObjType obj_type(Value value) {
    return (ObjType)(((Obj*)as_obj(value))->type);
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
// ObjClosure* new_closure(ObjFunction* function);
Symbol* new_string(const char* chars, int length);
// ObjList* new_list();

#endif
