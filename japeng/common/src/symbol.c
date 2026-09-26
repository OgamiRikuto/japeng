#include "symbol.h"
#include "object.h"

#include <stdlib.h>
#include <string.h>

#define POOL_MAX_LOAD 0.75

typedef struct {
    ObjString** entries;
    int count;
    int capacity;
} SymbolPool;

#define DEFINE_SYM(name, str) ObjString* sym_##name = NULL;
CORE_SYMBOLS(DEFINE_SYM)
#undef DEFINE_SYM

void init_symbols(void)
{
    #define INIT_SYM(name, str) sym_##name = intern_cstr(str);
        CORE_SYMBOLS(INIT_SYM)
    #undef INIT_SYM
}

static SymbolPool global_pool = {NULL, 0, 0};

// FNV-1a ハッシュアルゴリズム
static uint32_t hash_string(const char* key, int length)
{
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

static ObjString* find_interned(const char* chars, int length, uint32_t hash)
{
    if (global_pool.capacity == 0) return NULL;

    uint32_t mask = global_pool.capacity - 1;
    uint32_t index = hash & mask;

    for (;;) {
        ObjString* item = global_pool.entries[index];
        if (item == NULL) {
            return NULL;
        }

        if (item->length == length && item->hash == hash &&
            memcmp(item->chars, chars, length) == 0) {
            return item;
        }

        index = (index + 1) & mask;
    }
}

static void resize_pool(int new_capacity)
{
    ObjString** new_entries = (ObjString**)calloc(new_capacity, sizeof(ObjString*));
    uint32_t mask = new_capacity - 1;

    for (int i = 0; i < global_pool.capacity; i++) {
        ObjString* item = global_pool.entries[i];
        if (item == NULL) continue;

        uint32_t index = item->hash & mask;
        while (new_entries[index] != NULL) {
            index = (index + 1) & mask;
        }
        new_entries[index] = item;
    }

    free(global_pool.entries);
    global_pool.entries = new_entries;
    global_pool.capacity = new_capacity;
}

ObjString* intern_string(const char* chars, int length)
{
    uint32_t hash = hash_string(chars, length);
    ObjString* interned = find_interned(chars, length, hash);
    if (interned != NULL) {
        return interned;
    }

    if (global_pool.count + 1 > global_pool.capacity * POOL_MAX_LOAD) {
        int capacity = global_pool.capacity < 8 ? 8 : global_pool.capacity * 2;
        resize_pool(capacity);
    }
    
    ObjString* string = (ObjString*)malloc(sizeof(ObjString) + length + 1);
    string->header.type = OBJ_STRING;
    string->hash = hash;
    string->length = length;
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';

    uint32_t mask = global_pool.capacity - 1;
    uint32_t index = hash & mask;
    while (global_pool.entries[index] != NULL) {
        index = (index + 1) & mask;
    }

    global_pool.entries[index] = string;
    global_pool.count++;

    return string;
}

ObjString* intern_cstr(const char* str)
{
    return intern_string(str, (int)strlen(str));
}

void free_symbol_pool(void)
{
    for (int i = 0; i < global_pool.capacity; i++) {
        if (global_pool.entries[i] != NULL) {
            free(global_pool.entries[i]);
        }
    }
    free(global_pool.entries);
    global_pool.entries = NULL;
    global_pool.count = 0;
    global_pool.capacity = 0;
}
