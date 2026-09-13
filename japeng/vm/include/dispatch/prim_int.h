#ifndef PRIM_INT_H
#define PRIM_INT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct vm VM;

bool dispatch_integer(VM* vm, const char* method, uint8_t arg_count);
#endif
