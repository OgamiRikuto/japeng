#include "chunk.h"
#include "literal.h"
#include <stdlib.h>

void init_chunk(Chunk* chunk)
{
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->constant_count = 0;
    chunk->constant_capacity = 0;
    chunk->constants = NULL;
}

void free_chunk(Chunk* chunk)
{
    free(chunk->code);
    free(chunk->constants);
    init_chunk(chunk);
}

void write_chunk(Chunk* chunk, uint32_t instruction)
{   
    if (chunk->capacity < chunk->count + 1) {
        int old_capacity = chunk->capacity;
        chunk->capacity = (old_capacity < 8)? 8 : old_capacity * 2;
        chunk->code = (uint32_t*)realloc(chunk->code, sizeof(uint32_t) * chunk->capacity);
    }
    chunk->code[chunk->count++] = instruction;
}

int add_constant(Chunk* chunk, Value value)
{
    if (chunk->constant_capacity < chunk->constant_count + 1) {
        int old_capacity = chunk->constant_capacity;
        chunk->constant_capacity = (old_capacity < 8)? 8 : old_capacity * 2;
        chunk->constants = (Value*)realloc(chunk->constants, sizeof(Value) * chunk->capacity);
    }
    chunk->constants[chunk->constant_count] = value;
    return chunk->constant_count++;
}
