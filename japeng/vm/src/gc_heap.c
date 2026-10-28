#include "gc_heap.h"
#include "vm_class.h"
#include <stdlib.h>
#include <string.h>

void gc_heap_init(GCHeap* heap, size_t slot_capacity) {
    heap->capacity = slot_capacity;
    heap->allocated_count = 0;
    heap->remembered_count = 0;

    heap->slots = (Slot*)malloc(sizeof(Slot) * slot_capacity);
    heap->free_stack = (uint32_t*)malloc(sizeof(uint32_t) * slot_capacity);
    heap->free_top = 0;

    for (size_t i = 0; i < slot_capacity; i++) {
        heap->slots[i].header.flags = 0;
        heap->free_stack[heap->free_top++] = (uint32_t)i;
    }

    slab_init(&heap->slab);
}

void gc_heap_destroy(GCHeap* heap) {
    slab_free_all(&heap->slab);
    free(heap->slots);
    free(heap->free_stack);
}

Slot* gc_alloc_slot(GCHeap* heap, ObjType type) {
    if (heap->free_top <= 0) return NULL;

    uint32_t slot_idx = heap->free_stack[--heap->free_top];
    Slot* slot = &heap->slots[slot_idx];

    slot->header.type = (uint8_t)type;
    slot->header.flags = GC_FLAG_ALLOCATED;
    slot->header.slot_idx = slot_idx;
    heap->allocated_count++;

    return slot;
}

void gc_write_barrier(GCHeap* heap, void* parent_ptr, Value child_val) {
    if (!is_obj(child_val)) return;

    void* child_ptr = as_obj(child_val);
    if (!is_in_dynamic_heap(heap, parent_ptr) || !is_in_dynamic_heap(heap, child_ptr)) {
        return;
    }

    ObjHeader* parent = (ObjHeader*)parent_ptr;
    ObjHeader* child  = (ObjHeader*)child_ptr;

    uint8_t parent_age = (parent->flags & GC_AGE_MASK) >> GC_AGE_SHIFT;
    uint8_t child_age  = (child->flags & GC_AGE_MASK) >> GC_AGE_SHIFT;

    if (parent_age >= GC_AGE_MAX && child_age < GC_AGE_MAX) {
        if (!(parent->flags & GC_FLAG_REMEMBERED)) {
            if (heap->remembered_count < REMEMBERED_SET_MAX) {
                parent->flags |= GC_FLAG_REMEMBERED;
                heap->remembered_set[heap->remembered_count++] = parent;
            }
        }
    }
}

// ---------------------------------------------------------
// オブジェクト生成 API 群
// ---------------------------------------------------------
ObjInstance* gc_new_instance(GCHeap* heap, ObjClass* klass) {
    Slot* slot = gc_alloc_slot(heap, OBJ_INSTANCE);
    if (!slot) return NULL;
    ObjInstance* inst = &slot->instance;
    inst->klass = klass;

    uint32_t fc = (klass != NULL) ? (uint32_t)klass->field_count : 0;
    inst->field_count = fc;

    if (fc > 0) {
        size_t bytes = sizeof(Value) * fc;
        inst->fields = (Value*)slab_alloc(&heap->slab, bytes);
        if (klass->default_values != NULL) {
            memcpy(inst->fields, klass->default_values, bytes);
        } else {
            for (uint32_t i = 0; i < fc; i++) {
                inst->fields[i] = make_nil();
            }
        }
    } else {
        inst->fields = NULL;
    }
    return inst;
}

ObjString* gc_new_string(GCHeap* heap, const char* chars, size_t length) {
    Slot* slot = gc_alloc_slot(heap, OBJ_STRING);
    if (!slot) return NULL;
    ObjString* str = &slot->string;
    str->length = (uint8_t)length;
    str->hash = 0;

    if (length <= SSO_MAX_LEN) {
        str->is_inline = true;
        memcpy(str->as.inline_chars, chars, length);
        str->as.inline_chars[length] = '\0';
    } else {
        str->is_inline = false;
        str->as.heap.capacity = (uint32_t)(length + 1);
        str->as.heap.ptr = (char*)slab_alloc(&heap->slab, str->as.heap.capacity);
        memcpy(str->as.heap.ptr, chars, length);
        str->as.heap.ptr[length] = '\0';
    }
    return str;
}

ObjList* gc_new_list(GCHeap* heap, uint32_t initial_capacity) {
    Slot* slot = gc_alloc_slot(heap, OBJ_LIST);
    if (!slot) return NULL;
    ObjList* list = &slot->list;
    list->size = 0;
    list->capacity = (initial_capacity < 8) ? 8 : initial_capacity;
    list->elements = (Value*)slab_alloc(&heap->slab, sizeof(Value) * list->capacity);
    return list;
}

// Slab 対応のリスト拡張処理
void gc_list_grow(GCHeap* heap, ObjList* list) {
    uint32_t old_cap = list->capacity;
    if (old_cap < list->size + 1) {
        uint32_t new_cap = (old_cap < 8) ? 8 : old_cap * 2;
        Value* new_elems = (Value*)slab_alloc(&heap->slab, sizeof(Value) * new_cap);
        if (list->elements != NULL && list->size > 0) {
            memcpy(new_elems, list->elements, sizeof(Value) * list->size);
            slab_free(&heap->slab, list->elements, sizeof(Value) * old_cap);
        }
        list->elements = new_elems;
        list->capacity = new_cap;
    }
}

ObjClosure* gc_new_closure(GCHeap* heap, void* function, uint32_t capture_count) {
    Slot* slot = gc_alloc_slot(heap, OBJ_CLOSURE);
    if (!slot) return NULL;
    ObjClosure* closure = &slot->closure;
    closure->function = function;
    closure->capture_count = capture_count;
    if (capture_count > 0) {
        size_t bytes = sizeof(Value) * capture_count;
        closure->captures = (Value*)slab_alloc(&heap->slab, bytes);
        for (uint32_t i = 0; i < capture_count; i++) {
            closure->captures[i] = make_nil();
        }
    } else {
        closure->captures = NULL;
    }
    return closure;
}

ObjDecimal* gc_new_decimal(GCHeap* heap, BigInteger* mantissa, int32_t scale) {
    Slot* slot = gc_alloc_slot(heap, OBJ_DECIMAL);
    if (!slot) return NULL;
    ObjDecimal* dec = &slot->decimal;
    dec->mantissa = mantissa;
    dec->scale = (scale < 0) ? 0 : scale;
    return dec;
}
