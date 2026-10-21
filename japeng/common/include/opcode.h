#ifndef OPCODE_H
#define OPCODE_H

#include <stdint.h>

typedef enum {
    OP_CONSTANT,        // 0
    OP_GET_LOCAL,       // 1
    OP_SET_LOCAL,       // 2
    OP_GET_GLOBAL,      // 3
    OP_SET_GLOBAL,
    OP_GET_FIELD,
    OP_SET_FIELD,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,
    OP_SEND,
    OP_SUPER_CALL,
    OP_CALL,
    OP_POP,
    OP_DUP,
    OP_RETURN,
    OP_NEW_INSTANCE,
    OP_CLOSURE,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
    OP_CONCAT,
    OP_ADD_INT,
    OP_SUB_INT,
    OP_MUL_INT,
    OP_DIV_INT,
    OP_REM_INT,
    OP_ADD_FLOAT,
    OP_SUB_FLOAT,
    OP_MUL_FLOAT,
    OP_DIV_FLOAT,
    OP_ADD_DEC,
    OP_SUB_DEC,
    OP_MUL_DEC,
    OP_DIV_DEC,
    OP_LESS,
    OP_GREAT,
    OP_EQUAL,
    OP_NEQUAL,
    OP_NGREAT,
    OP_NLESS,
    OP_EQUAL_INT,
    OP_NEQUAL_INT,
    OP_LESS_INT,
    OP_GREAT_INT,
    OP_NLESS_INT,
    OP_NGREAT_INT,
    OP_SHL,
    OP_SHR,
    OP_BAND,
    OP_BOR,
    OP_BXOR,
    OP_BNOT,
    OP_NOT,
    OP_MAX
} Opcode;

/* 命令を作成する */
static inline uint32_t make_inst(Opcode op, uint32_t operand) {
    return ((uint32_t)op << 24) | (operand & 0x00FFFFFF);
}

static inline uint32_t make_send_inst(uint8_t arg_count, uint16_t msg_index) {
    return ((uint32_t)OP_SEND << 24) | ((uint32_t)arg_count << 16) | (msg_index & 0xFFFF);
}

static inline uint32_t make_super_call_inst(uint8_t arg_count, uint16_t fn_index) {
    return ((uint32_t)OP_SUPER_CALL << 24) | ((uint32_t)arg_count << 16) | (fn_index & 0xFFFF);
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
