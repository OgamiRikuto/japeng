#ifndef BUILTINS_H
#define BUILTINS_H

#include "vm.h"
#include <stdint.h>
#include <stdbool.h>

#include "builtin_functions/list.h"

void init_builtin_classes(VM* vm);
bool native_println(VM* vm, uint8_t arg_count);
bool native_print(VM* vm, uint8_t arg_count);
#endif
