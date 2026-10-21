#ifndef BUILTINS_H
#define BUILTINS_H

#include "vm.h"
#include "native.h"
#include "common.h"
#include <stdint.h>
#include <stdbool.h>

void define_native(ObjClass* klass, const char* name, NativeFn func);
void init_builtin_classes(VM* vm);

void setup_print(VM* vm, ObjClass* klass);
void setup_number(VM* vm, TypeInfo* o_info);
void setup_boolean(VM* vm, TypeInfo* o_info);
void list_append(ObjList* list, Value val);
void setup_list(VM* vm, TypeInfo* o_info);
void setup_string(VM* vm, TypeInfo* o_info);
#endif
