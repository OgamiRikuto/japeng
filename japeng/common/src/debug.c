// #include <stdio.h>
// #include "opcode.h"
// #include "chunk.h"

// void disassemble_instruction(Chunk* chunk, int offset) {
//     uint32_t inst = chunk->code[offset];
//     Opcode op = get_op(inst);

//     printf("%04d  ", offset);

//     switch (op) {
//         case OP_CONSTANT: {
//             uint32_t index = get_operand(inst);
//             printf("%-16s %4u '", "OP_CONSTANT", index);
//             // 定数プールから値を取り出して表示（Valueのダンプ）
//             print_value(chunk->constants[index]);
//             printf("'\n");
//             break;
//         }
//         case OP_GET_LOCAL:
//             printf("%-16s %4u\n", "OP_GET_LOCAL", get_operand(inst));
//             break;
//         case OP_SET_LOCAL:
//             printf("%-16s %4u\n", "OP_SET_LOCAL", get_operand(inst));
//             break;
//         case OP_SEND: {
//             uint8_t args = get_send_args(inst);
//             uint16_t msg_idx = get_send_index(inst);
//             printf("%-16s args:%-3u msg_idx:%-4u\n", "OP_SEND", args, msg_idx);
//             break;
//         }
//         case OP_POP:
//             printf("%-16s\n", "OP_POP");
//             break;
//         case OP_RETURN:
//             printf("%-16s\n", "OP_RETURN");
//             break;
//         default:
//             printf("Unknown opcode %d\n", op);
//             break;
//     }
// }

// void disassemble_chunk(Chunk* chunk, const char* name) {
//     printf("== %s ==\n", name);
//     for (int offset = 0; offset < chunk->count; offset++) {
//         disassemble_instruction(chunk, offset);
//     }
// }
