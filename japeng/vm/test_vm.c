#include <stdio.h>

#include "vm.h"
#include "common.h"

int main(void) {
    VM vm;
    init_vm(&vm);

    Chunk chunk;
    init_chunk(&chunk);

    // 定数 42 をプールに追加
    int const_idx = add_constant(&chunk, make_int(42));

    // 42 をスタックに積み、それを return する
    write_chunk(&chunk, make_inst(OP_CONSTANT, const_idx));
    write_chunk(&chunk, make_inst(OP_RETURN, 0));

    // VM で実行
    InterpretResult result = interpret(&vm, &chunk);

    free_chunk(&chunk);
    free_vm(&vm);

    return (result == INTERPRET_OK) ? 0 : 1;
}
