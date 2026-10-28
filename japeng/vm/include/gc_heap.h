#ifndef GC_HEAP_H
#define GC_HEAP_H

#include "gc_layout.h"
#include "gc_slab.h"
#include "vm_class.h"

#define REMEMBERED_SET_MAX 1024

typedef struct {
    Slot*          slots;            // 32B × capacity の一撃ドカン
    uint32_t*      free_stack;       // 空きスロットLIFOスタック
    int            free_top;
    size_t         capacity;
    size_t         allocated_count;

    SlabAllocator  slab;             // 外部バッファ用 Slab

    // 世代別GC用 Remembered Set
    ObjHeader*     remembered_set[REMEMBERED_SET_MAX];
    int            remembered_count;
} GCHeap;

void         gc_heap_init(GCHeap* heap, size_t slot_capacity);
void         gc_heap_destroy(GCHeap* heap);
Slot*        gc_alloc_slot(GCHeap* heap, ObjType type);
void         gc_write_barrier(GCHeap* heap, void* parent_ptr, Value child_val);

// 各オブジェクトのファクトリ
ObjInstance* gc_new_instance(GCHeap* heap, ObjClass* klass);
ObjString*   gc_new_string(GCHeap* heap, const char* chars, size_t length);
ObjList*     gc_new_list(GCHeap* heap, uint32_t initial_capacity);
void         gc_list_grow(GCHeap* heap, ObjList* list);
ObjClosure*  gc_new_closure(GCHeap* heap, void* function, uint32_t capture_count);
ObjDecimal*  gc_new_decimal(GCHeap* heap, BigInteger* mantissa, int32_t scale);

static inline bool is_in_dynamic_heap(const GCHeap* heap, const void* ptr) {
    const Slot* target = (const Slot*)ptr;
    return (target >= heap->slots && target < (heap->slots + heap->capacity));
}

#endif // GC_HEAP_H
