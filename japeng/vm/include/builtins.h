#ifndef BUILTINS_H
#define BUILTINS_H

#include "vm.h"
#include "native.h"
#include "common.h"
#include <stdint.h>
#include <stdbool.h>

// void define_native(ObjClass* klass, const char* name, NativeFn func);
void init_builtin_classes(VM* vm);

void setup_print(VM* vm, ObjClass* klass);
void setup_number(VM* vm);
void setup_boolean(VM* vm);
void list_append(VM* vm, ObjList* list, Value val);
void setup_list(VM* vm);
void setup_string(VM* vm);

void set_static_int(ObjClass* klass, const char* name, int32_t val);
void vm_define_static_native(ObjClass* klass, const char* name, NativeFn func);
void register_file_natives(ObjClass* klass);
void register_clock_natives(ObjClass* klass);
void register_random_natives(ObjClass* klass);

void register_window_natives(ObjClass* klass);
void register_input_natives(ObjClass* klass);
void register_key_natives(ObjClass* klass);
#endif
