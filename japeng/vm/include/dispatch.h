#ifndef DISPATCH_H
#define DISPATCH_H

#include <stdbool.h>
#include <stdint.h>

typedef struct vm VM;
typedef uint64_t Value;

bool dispatch_send(VM* vm, uint8_t arg_count, uint16_t msg_index);
#endif
