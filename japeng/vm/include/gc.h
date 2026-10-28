#ifndef GC_H
#define GC_H

#include "gc_layout.h"
#include "gc_heap.h"
#include "gc_slab.h"

typedef struct vm VM;

void gc_collect_minor(VM* vm);
void gc_collect_full(VM* vm);

#endif // GC_H
