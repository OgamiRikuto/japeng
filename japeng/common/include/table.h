#ifndef TABLE_H
#define TABLE_H

#include "object.h"
#include "literal.h"
#include <stdbool.h>

typedef enum {
    UNUSED,
    OCCUPIED,
    DELETED
} EntryStatus;

typedef struct {
    ObjString* key;
    Value value;
    EntryStatus status;
} HashEntry;

typedef struct table {
    HashEntry* entries;
    int capacity;
    int count;
} Table;

Table* new_table(int init_capacity);
void table_free(Table* self);
bool table_set(Table* self, ObjString* key, Value value);
bool table_get(Table* self, ObjString* key, Value* out_value);
bool table_delete(Table* self, ObjString* key);

#endif
