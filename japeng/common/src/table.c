#include "table.h"

#include <stdlib.h>

#define TABLE_MAX_LOAD 0.7

Table* new_table(int init_capacity)
{
    int capacity = 8;
    while (capacity < init_capacity) capacity *= 2;

    Table* self = (Table*)malloc(sizeof(Table));
    self->capacity = capacity;
    self->count = 0;
    self->entries = (HashEntry*)malloc(sizeof(HashEntry) * capacity);

    for (int i = 0; i < capacity; i++) {
        self->entries[i].key = NULL;
        self->entries[i].value = make_nil();
        self->entries[i].status = UNUSED;
    }
    return self;
}

static void resize(Table* self, int new_capacity)
{
    HashEntry* old_entries = self->entries;
    int old_capacity = self->capacity;

    self->capacity = new_capacity;
    self->count = 0;
    self->entries = (HashEntry*)malloc(sizeof(HashEntry) * new_capacity);

    for (int i = 0; i < new_capacity; i++) {
        self->entries[i].key = NULL;
        self->entries[i].value = make_nil();
        self->entries[i].status = UNUSED;
    }

    for (int i = 0; i < old_capacity; i++) {
        if (old_entries[i].status == OCCUPIED) {
            table_set(self, old_entries[i].key, old_entries[i].value);
        }
    }
    free(old_entries);
}

bool table_set(Table* self, ObjString* key, Value value)
{
    if ((double)(self->count + 1) / self->capacity > TABLE_MAX_LOAD) {
        resize(self, self->capacity * 2);
    }

    uint32_t mask = self->capacity - 1;
    uint32_t index = key->hash & mask;
    int first_deleted = -1;

    while (1) {
        HashEntry* entry = &self->entries[index];

        if (entry->status == UNUSED) {
            int target = (first_deleted != -1) ? first_deleted : index;
            self->entries[target].key = key;
            self->entries[target].value = value;
            self->entries[target].status = OCCUPIED;
            self->count++;
            return false;
        }

        if (entry->status == DELETED) {
            if (first_deleted == -1) first_deleted = index;
        } else if (entry->key == key) {
            entry->value = value;
            return true;
        }

        index = (index + 1) & mask;
    }
}

bool table_get(Table* self, ObjString* key, Value* out_value)
{
    if (self->count == 0) return false;

    uint32_t mask = self->capacity - 1;
    uint32_t index = key->hash & mask;

    while (1) {
        HashEntry* entry = &self->entries[index];

        if (entry->status == UNUSED) {
            return false;
        }

        if (entry->status == OCCUPIED && entry->key == key) {
            *out_value = entry->value;
            return true;
        }

        index = (index + 1) & mask;
    }
}

bool table_delete(Table* self, ObjString* key)
{
    if (self->count == 0) return false;

    uint32_t mask = self->capacity - 1;
    uint32_t index = key->hash & mask;

    while (1) {
        HashEntry* entry = &self->entries[index];

        if (entry->status == UNUSED) {
            return false;
        }

        if (entry->status == OCCUPIED && entry->key == key) {
            entry->status = DELETED;
            entry->key = NULL;
            entry->value = make_nil();
            self->count--;
            return true;
        }

        index = (index + 1) & mask;
    }
}

void table_free(Table* self)
{
    if (self == NULL) return;
    free(self->entries);
    free(self);
}
