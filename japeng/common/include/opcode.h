#ifndef OPCODE_H
#define OPCODE_H

#include <stdint.h>

typedef enum {
    OP_CONSTANT,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,
    OP_GET_FIELD,
    OP_SET_FIELD,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,
    OP_SEND,
    OP_CALL,
    OP_POP,
    OP_DUP,
    OP_RETURN,
    OP_NEW_INSTANCE,
    OP_CLOSURE,
    OP_GET_UPVALUE,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_LESS,
    OP_GREAT,
    OP_EQUAL,
    OP_MAX
} Opcode;

/* 命令を作成する */
static inline uint32_t make_inst(Opcode op, uint32_t operand) {
    return ((uint32_t)op << 24) | (operand & 0x00FFFFFF);
}

static inline uint32_t make_send_inst(uint8_t arg_count, uint16_t msg_index) {
    return ((uint32_t)OP_SEND << 24) | ((uint32_t)arg_count << 16) | (msg_index & 0xFFFF);
}

/* 命令を取り出す */
static inline Opcode get_op(uint32_t instruction) {
    return (Opcode)(instruction >> 24);
}

static inline uint32_t get_operand(uint32_t instruction) {
    return (instruction & 0x00FFFFFF);
}

static inline uint8_t get_send_args(uint32_t instruction) {
    return ((uint8_t)(instruction >> 16) & 0xFF);
}

static inline uint16_t get_send_index(uint32_t instruction) {
    return ((uint16_t)instruction & 0xFFFF);
}
#endif
