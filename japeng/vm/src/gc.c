#include "gc.h"
#include "vm.h"

static void mark_object(GCHeap* heap, void* ptr, bool is_minor) {
    if (ptr == NULL) return;
    if (!is_in_dynamic_heap(heap, ptr)) return;

    Slot* slot = (Slot*)ptr;
    ObjHeader* header = &slot->header;
    if (!(header->flags & GC_FLAG_ALLOCATED)) return;

    uint8_t age = (header->flags & GC_AGE_MASK) >> GC_AGE_SHIFT;
    if (is_minor && age >= GC_AGE_MAX) {
        return;
    }

    if (header->flags & GC_FLAG_MARK) return;
    header->flags |= GC_FLAG_MARK;

    switch (header->type) {
        case OBJ_INSTANCE: {
            ObjInstance* inst = (ObjInstance*)slot;
            for (uint32_t i = 0; i < inst->field_count; i++) { // ★ 全フィールド走査
                if (is_obj(inst->fields[i])) {
                    mark_object(heap, as_obj(inst->fields[i]), is_minor);
                }
            }
            break;
        }
        case OBJ_LIST: {
            ObjList* list = (ObjList*)slot;
            for (uint32_t i = 0; i < list->size; i++) { // ★ size に修正
                if (is_obj(list->elements[i])) {
                    mark_object(heap, as_obj(list->elements[i]), is_minor);
                }
            }
            break;
        }
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)slot;
            for (uint32_t i = 0; i < closure->capture_count; i++) {
                if (is_obj(closure->captures[i])) {
                    mark_object(heap, as_obj(closure->captures[i]), is_minor);
                }
            }
            break;
        }
        case OBJ_STRING:
        case OBJ_BIGINTEGER: // ★ OBJ_BIGINTEGER に修正
        case OBJ_DECIMAL:
            break;
        default:
            break;
    }
}

static void mark_roots(VM* vm, bool is_minor) {
    for (Value* slot = vm->stack; slot < vm->stack_top; slot++) {
        if (is_obj(*slot)) {
            mark_object(&vm->heap, as_obj(*slot), is_minor);
        }
    }

    for (int i = 0; i < vm->frame_count; i++) {
        if (vm->frames[i].closure != NULL) {
            mark_object(&vm->heap, vm->frames[i].closure, is_minor);
        }
    }

    if (is_minor) {
        for (int i = 0; i < vm->heap.remembered_count; i++) {
            ObjHeader* old_obj = vm->heap.remembered_set[i];
            if (old_obj && (old_obj->flags & GC_FLAG_ALLOCATED)) {
                if (old_obj->type == OBJ_INSTANCE) {
                    ObjInstance* inst = (ObjInstance*)old_obj;
                    for (uint32_t f = 0; f < inst->field_count; f++) { // ★ field_count 走査
                        if (is_obj(inst->fields[f])) {
                            mark_object(&vm->heap, as_obj(inst->fields[f]), is_minor);
                        }
                    }
                } else if (old_obj->type == OBJ_LIST) {
                    ObjList* list = (ObjList*)old_obj;
                    for (uint32_t e = 0; e < list->size; e++) { // ★ size に修正
                        if (is_obj(list->elements[e])) {
                            mark_object(&vm->heap, as_obj(list->elements[e]), is_minor);
                        }
                    }
                }
            }
        }
    }
}

static void free_external_buffer(GCHeap* heap, Slot* slot) {
    ObjHeader* obj = &slot->header;
    switch (obj->type) {
        case OBJ_STRING: {
            ObjString* str = (ObjString*)slot;
            if (!str->is_inline && str->as.heap.ptr != NULL) {
                slab_free(&heap->slab, str->as.heap.ptr, str->as.heap.capacity);
                str->as.heap.ptr = NULL;
            }
            break;
        }
        case OBJ_LIST: {
            ObjList* list = (ObjList*)slot;
            if (list->elements != NULL) {
                slab_free(&heap->slab, list->elements, sizeof(Value) * list->capacity);
                list->elements = NULL;
            }
            break;
        }
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)slot;
            if (closure->captures != NULL) {
                slab_free(&heap->slab, closure->captures, sizeof(Value) * closure->capture_count);
                closure->captures = NULL;
            }
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance* inst = (ObjInstance*)slot;
            if (inst->fields != NULL) {
                slab_free(&heap->slab, inst->fields, sizeof(Value) * inst->field_count);
                inst->fields = NULL;
            }
            break;
        }
        case OBJ_DECIMAL: {
            ObjDecimal* dec = (ObjDecimal*)slot;
            if (dec->mantissa != NULL) {
                free_bi(dec->mantissa);
                dec->mantissa = NULL;
            }
            break;
        }
        default:
            break;
    }
}

static void sweep(GCHeap* heap, bool is_minor) {
    for (size_t i = 0; i < heap->capacity; i++) {
        Slot* slot = &heap->slots[i];
        ObjHeader* header = &slot->header;

        if (!(header->flags & GC_FLAG_ALLOCATED)) continue;

        uint8_t age = (header->flags & GC_AGE_MASK) >> GC_AGE_SHIFT;
        if (is_minor && age >= GC_AGE_MAX) {
            continue;
        }

        if (header->flags & GC_FLAG_MARK) {
            header->flags &= ~GC_FLAG_MARK;
            if (age < GC_AGE_MAX) {
                age++;
                header->flags = (header->flags & ~GC_AGE_MASK) | (age << GC_AGE_SHIFT);
            }
        } else {
            free_external_buffer(heap, slot);
            header->flags = 0;
            heap->allocated_count--;
            heap->free_stack[heap->free_top++] = (uint32_t)i;
        }
    }

    int new_rem_count = 0;
    for (int i = 0; i < heap->remembered_count; i++) {
        ObjHeader* old_obj = heap->remembered_set[i];
        if (old_obj->flags & GC_FLAG_ALLOCATED) {
            heap->remembered_set[new_rem_count++] = old_obj;
        } else {
            old_obj->flags &= ~GC_FLAG_REMEMBERED;
        }
    }
    heap->remembered_count = new_rem_count;
}

void gc_collect_minor(VM* vm) {
    mark_roots(vm, true);
    sweep(&vm->heap, true);
}

void gc_collect_full(VM* vm) {
    for (int i = 0; i < vm->heap.remembered_count; i++) {
        vm->heap.remembered_set[i]->flags &= ~GC_FLAG_REMEMBERED;
    }
    vm->heap.remembered_count = 0;

    mark_roots(vm, false);
    sweep(&vm->heap, false);
}
