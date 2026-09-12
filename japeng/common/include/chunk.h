#ifndef CHUNK_H
#define CHUNK_H

#include <stdint.h>

typedef uint64_t Value;

typedef struct chunk{
    int count;
    int capacity;
    uint32_t* code;

    int constant_count;
    int constant_capacity;
    Value* constants;
} Chunk;

void init_chunk(Chunk* chunk);
void free_chunk(Chunk* chunk);
void write_chunk(Chunk* chunk, uint32_t instruction);
int add_constant(Chunk* chunk, Value value);

#endif
