#ifndef BUILTINS_H
#define BUILTINS_H

#include "vm.h"
#include "native.h"
#include "common.h"
#include <stdint.h>
#include <stdbool.h>

void define_native(ObjClass* klass, const char* name, NativeFn func);
void init_builtin_classes(VM* vm);
// bool native_println(VM* vm, uint8_t arg_count);
// bool native_print(VM* vm, uint8_t arg_count);
void setup_print(VM* vm, ObjClass* klass);
void setup_list(VM* vm, ObjClass* super_class, TypeInfo* t_info);
void setup_string(VM* vm, ObjClass* super_class, TypeInfo* t_info);
#endif
