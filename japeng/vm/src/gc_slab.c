#include "gc_slab.h"
#include <stdlib.h>

#define SLAB_CAPACITY_64  2048 // 64B  × 2048 = 128KB
#define SLAB_CAPACITY_128 1024 // 128B × 1024 = 128KB
#define SLAB_CAPACITY_256  512 // 256B ×  512 = 128KB

static void init_single_pool(SlabPool* pool, size_t block_size, size_t capacity) {
    pool->block_size = block_size;
    pool->capacity = capacity;
    pool->pool = (uint8_t*)malloc(block_size * capacity);
    pool->free_stack = (uint32_t*)malloc(sizeof(uint32_t) * capacity);
    pool->free_top = 0;

    for (size_t i = 0; i < capacity; i++) {
        pool->free_stack[pool->free_top++] = (uint32_t)i;
    }
}

void slab_init(SlabAllocator* slab) {
    init_single_pool(&slab->pool_64, 64, SLAB_CAPACITY_64);
    init_single_pool(&slab->pool_128, 128, SLAB_CAPACITY_128);
    init_single_pool(&slab->pool_256, 256, SLAB_CAPACITY_256);
}

void slab_free_all(SlabAllocator* slab) {
    free(slab->pool_64.pool);
    free(slab->pool_64.free_stack);
    free(slab->pool_128.pool);
    free(slab->pool_128.free_stack);
    free(slab->pool_256.pool);
    free(slab->pool_256.free_stack);
}

static inline bool is_in_pool(const SlabPool* pool, const void* ptr) {
    const uint8_t* p = (const uint8_t*)ptr;
    return (p >= pool->pool && p < (pool->pool + pool->block_size * pool->capacity));
}

void* slab_alloc(SlabAllocator* slab, size_t size) {
    SlabPool* target = NULL;
    if (size <= 64)       target = &slab->pool_64;
    else if (size <= 128) target = &slab->pool_128;
    else if (size <= 256) target = &slab->pool_256;

    if (target != NULL) {
        if (target->free_top > 0) {
            uint32_t idx = target->free_stack[--target->free_top];
            return (void*)(target->pool + (idx * target->block_size));
        }
        // プールが満杯なら libc malloc へフォールバック
    }
    return malloc(size);
}

void slab_free(SlabAllocator* slab, void* ptr, size_t size) {
    if (ptr == NULL) return;

    SlabPool* target = NULL;
    if (size <= 64)       target = &slab->pool_64;
    else if (size <= 128) target = &slab->pool_128;
    else if (size <= 256) target = &slab->pool_256;

    if (target != NULL && is_in_pool(target, ptr)) {
        uint32_t idx = (uint32_t)(((uint8_t*)ptr - target->pool) / target->block_size);
        target->free_stack[target->free_top++] = idx;
        return;
    }

    free(ptr);
}
