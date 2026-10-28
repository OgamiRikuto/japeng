#ifndef GC_SLAB_H
#define GC_SLAB_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    size_t    block_size;  // 64, 128, 256
    size_t    capacity;    // 保持できるブロック数
    uint8_t*  pool;        // 生バッファ (block_size * capacity)
    uint32_t* free_stack;  // 空きインデックススタック
    int       free_top;
} SlabPool;

typedef struct {
    SlabPool pool_64;
    SlabPool pool_128;
    SlabPool pool_256;
} SlabAllocator;

void  slab_init(SlabAllocator* slab);
void  slab_free_all(SlabAllocator* slab);
void* slab_alloc(SlabAllocator* slab, size_t size);
void  slab_free(SlabAllocator* slab, void* ptr, size_t size);

#endif // GC_SLAB_H
