#include <stdio.h>
#include <stdlib.h>
#include "chunk.h"
#include "opcode.h"
#include "literal.h"

// テスト用：Value の簡易表示関数
static void print_value(Value value) {
    if (is_int(value)) {
        printf("%d", as_int(value));
    } else if (is_float(value)) {
        printf("%g", as_float(value));
    } else {
        printf("<object>");
    }
}

// 1命令のディスアセンブル
void disassemble_instruction(Chunk* chunk, int offset) {
    uint32_t inst = chunk->code[offset];
    Opcode op = get_op(inst);

    printf("%04d  ", offset);

    switch (op) {
        case OP_CONSTANT: {
            uint32_t index = get_operand(inst);
            printf("%-16s index:%-4u '", "OP_CONSTANT", index);
            print_value(chunk->constants[index]);
            printf("'\n");
            break;
        }
        case OP_GET_LOCAL:
            printf("%-16s slot:%-4u\n", "OP_GET_LOCAL", get_operand(inst));
            break;
        case OP_SET_LOCAL:
            printf("%-16s slot:%-4u\n", "OP_SET_LOCAL", get_operand(inst));
            break;
        case OP_SEND: {
            uint8_t args = get_send_args(inst);
            uint16_t msg_idx = get_send_index(inst);
            printf("%-16s args:%-3u msg_idx:%-4u\n", "OP_SEND", args, msg_idx);
            break;
        }
        case OP_POP:
            printf("%-16s\n", "OP_POP");
            break;
        case OP_RETURN:
            printf("%-16s\n", "OP_RETURN");
            break;
        default:
            printf("Unknown opcode %d\n", op);
            break;
    }
}

// Chunk 全体のディスアセンブル
void disassemble_chunk(Chunk* chunk, const char* name) {
    printf("== %s ==\n", name);
    for (int offset = 0; offset < chunk->count; offset++) {
        disassemble_instruction(chunk, offset);
    }
}

int main(void) {
    Chunk chunk;
    init_chunk(&chunk);

    // 1. 定数を追加（例: 10 と 20）
    int const_10 = add_constant(&chunk, make_int(10));
    int const_20 = add_constant(&chunk, make_int(20));

    // 2. バイトコードを書き込む (10 と 20 を積んで引数1でメッセージ5番を送信する想定)
    write_chunk(&chunk, make_inst(OP_CONSTANT, const_10));
    write_chunk(&chunk, make_inst(OP_CONSTANT, const_20));
    write_chunk(&chunk, make_send_inst(1, 5)); // 引数1個、メッセージインデックス5番
    write_chunk(&chunk, make_inst(OP_RETURN, 0));

    // 3. バイトコードの内容をダンプ出力
    disassemble_chunk(&chunk, "Test Chunk");

    // 4. メモリ解放
    free_chunk(&chunk);

    return 0;
}
