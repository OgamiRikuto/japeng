#ifndef __LITERAL_H__
#define __LITERAL_H__
#include <stdint.h>
#include <stdbool.h>

typedef uint64_t Value;

#define NAN_MASK 0x7FF8000000000000ULL

#define TAG_NIL 1ULL
#define TAG_BOOL 2ULL
#define TAG_INT 3ULL
#define TAG_OBJ 4ULL

// 生成
static inline Value make_float(double num) {
    union { double d; uint64_t u; } conv;
    conv.d = num;
    return conv.u;
}

static inline Value make_nil() {
    return NAN_MASK | (TAG_NIL << 48);
}

static inline Value make_bool(bool b) {
    return NAN_MASK | (TAG_BOOL << 48) | (b ? 1ULL : 0ULL);
}
static inline Value make_int(int32_t i) {
    return NAN_MASK | (TAG_INT << 48) | (uint32_t) i;
}

static inline Value make_obj(void* ptr) {
    return NAN_MASK | (TAG_OBJ << 48) | (uint64_t)(uintptr_t)ptr;
}

// チェック
static inline bool is_float(Value v) {
    return (v & NAN_MASK) != NAN_MASK;
}

#define CHECK_TAG(v, tag) \
    (((v) & (NAN_MASK | (7ULL << 48))) == (NAN_MASK | ((tag) << 48)))

static inline bool is_nil(Value v) { return CHECK_TAG(v, TAG_NIL); }
static inline bool is_bool(Value v) { return CHECK_TAG(v, TAG_BOOL); }
static inline bool is_int(Value v) { return CHECK_TAG(v, TAG_INT); }
static inline bool is_obj(Value v) { return CHECK_TAG(v, TAG_OBJ); }

// 元に戻す
static inline double as_float(Value v) {
    union { uint64_t u; double d; } conv;
    conv.u = v;
    return conv.d;
}

static inline bool as_bool(Value v) {
    return (v & 1ULL) != 0;
}

static inline int32_t as_int(Value v) {
    return (int32_t)(v & 0xFFFFFFFFULL);
}

static inline void* as_obj(Value v) {
    return (void*)(uintptr_t)(v & 0xFFFFFFFFFFFFULL); 
}

#endif
