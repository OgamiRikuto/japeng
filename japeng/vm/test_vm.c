#include <stdio.h>
#include <stdbool.h>

#include "vm.h"
#include "common.h"

static bool test_stack(VM* vm)
{
    Chunk chunk;
    init_chunk(&chunk);

    // 定数 42 をプールに追加
    int const_idx = add_constant(&chunk, make_int(42));

    // 42 をスタックに積み、それを return する
    write_chunk(&chunk, make_inst(OP_CONSTANT, const_idx));
    write_chunk(&chunk, make_inst(OP_RETURN, 0));

    // VM で実行
    InterpretResult result = interpret(vm, &chunk);

    free_chunk(&chunk);
    free_vm(vm);

    return (result == INTERPRET_OK) ? 0 : 1;
}

static bool test_add(VM* vm)
{
    Chunk chunk;
    init_chunk(&chunk);

    // 1. 定数プールにデータを用意
    int const_10  = add_constant(&chunk, make_int(10));
    int const_32  = add_constant(&chunk, make_int(32));
    int msg_plus  = add_constant(&chunk, make_obj((void*)"+")); // メソッド名 "+"

    // 2. バイトコード生成: 10 + 32 .
    // レシーバを積む
    write_chunk(&chunk, make_inst(OP_CONSTANT, const_10));
    // 引数を積む
    write_chunk(&chunk, make_inst(OP_CONSTANT, const_32));
    // OP_SEND: 引数1個, メッセージ名 msg_plus
    write_chunk(&chunk, make_send_inst(1, msg_plus));
    // 計算結果を返して終了
    write_chunk(&chunk, make_inst(OP_RETURN, 0));

    // 3. 実行
    InterpretResult result = interpret(vm, &chunk);

    free_chunk(&chunk);
    free_vm(vm);

    return (result == INTERPRET_OK) ? 0 : 1;
}

static bool test_local(VM* vm)
{
    Chunk chunk;
    init_chunk(&chunk);

    // 定数登録
    int const_10 = add_constant(&chunk, make_int(10));
    int const_32 = add_constant(&chunk, make_int(32));
    int msg_plus = add_constant(&chunk, make_obj((void*)"+"));

    // 1. ローカル変数 x (スロット0) に 10 を代入
    write_chunk(&chunk, make_inst(OP_CONSTANT, const_10));
    // (スタックに 10 が積まれ、これがスロット0になる)

    // 2. ローカル変数 y (スロット1) に 32 を代入
    write_chunk(&chunk, make_inst(OP_CONSTANT, const_32));
    // (スタックに 32 が積まれ、これがスロット1になる)

    // 3. x (スロット0) を読み出す
    write_chunk(&chunk, make_inst(OP_GET_LOCAL, 0));

    // 4. y (スロット1) を読み出す
    write_chunk(&chunk, make_inst(OP_GET_LOCAL, 1));

    // 5. メッセージ送信: x + y (引数1個)
    write_chunk(&chunk, make_send_inst(1, msg_plus));

    // 6. 結果を返して終了
    write_chunk(&chunk, make_inst(OP_RETURN, 0));

    // 実行
    InterpretResult result = interpret(vm, &chunk);

    free_chunk(&chunk);
    free_vm(vm);

    return (result == INTERPRET_OK) ? 0 : 1;
}

static bool test_if(VM* vm)
{
    Chunk chunk;
    init_chunk(&chunk);

    // 定数の登録
    int const_false = add_constant(&chunk, make_bool(false));
    int const_100   = add_constant(&chunk, make_int(100));
    int const_200   = add_constant(&chunk, make_int(200));

    // [0] 条件式 (false) をプッシュ
    write_chunk(&chunk, make_inst(OP_CONSTANT, const_false));

    // [1] falseなら 2命令スキップして [4] へジャンプ（[2]と[3]を飛ばす）
    write_chunk(&chunk, make_inst(OP_JUMP_IF_FALSE, 2));

    // [2] thenブロック: 100 をプッシュ
    write_chunk(&chunk, make_inst(OP_CONSTANT, const_100));

    // [3] then終了時: elseブロックを飛び越えて [5] へジャンプ（[4]を飛ばす）
    write_chunk(&chunk, make_inst(OP_JUMP, 1));

    // [4] elseブロック: 200 をプッシュ
    write_chunk(&chunk, make_inst(OP_CONSTANT, const_200));

    // [5] 終了
    write_chunk(&chunk, make_inst(OP_RETURN, 0));

    InterpretResult result = interpret(vm, &chunk);

    free_chunk(&chunk);
    free_vm(vm);

    return (result == INTERPRET_OK) ? 0 : 1;
}

static bool test_loop(VM* vm)
{
    Chunk chunk;
    init_chunk(&chunk);

    // 1. 定数の登録
    int const_0     = add_constant(&chunk, make_int(0));
    int const_5     = add_constant(&chunk, make_int(5));
    int const_1     = add_constant(&chunk, make_int(1));
    int msg_plus    = add_constant(&chunk, make_obj((void*)"+"));
    int msg_minus   = add_constant(&chunk, make_obj((void*)"-"));
    int msg_gt      = add_constant(&chunk, make_obj((void*)">"));

    // 2. 変数初期化
    // スロット0: sum = 0
    write_chunk(&chunk, make_inst(OP_CONSTANT, const_0));
    // スロット1: n = 5
    write_chunk(&chunk, make_inst(OP_CONSTANT, const_5));

    // [Index: 2] --- ループ先頭 ---
    // 条件判定: n > 0
    write_chunk(&chunk, make_inst(OP_GET_LOCAL, 1));       // [2] n
    write_chunk(&chunk, make_inst(OP_CONSTANT, const_0));    // [3] 0
    write_chunk(&chunk, make_send_inst(1, msg_gt));          // [4] n > 0

    // [5] 偽ならループ脱出 (脱出先は [17] なのでオフセットは 17 - 6 = 11)
    write_chunk(&chunk, make_inst(OP_JUMP_IF_FALSE, 11));

    // --- ループ本体 ---
    // sum = sum + n
    write_chunk(&chunk, make_inst(OP_GET_LOCAL, 0));       // [6] sum
    write_chunk(&chunk, make_inst(OP_GET_LOCAL, 1));       // [7] n
    write_chunk(&chunk, make_send_inst(1, msg_plus));      // [8] sum + n
    write_chunk(&chunk, make_inst(OP_SET_LOCAL, 0));       // [9] sum に格納
    write_chunk(&chunk, make_inst(OP_POP, 0));             // [10] 代入式の余剰値を破棄

    // n = n - 1
    write_chunk(&chunk, make_inst(OP_GET_LOCAL, 1));       // [11] n
    write_chunk(&chunk, make_inst(OP_CONSTANT, const_1));   // [12] 1
    write_chunk(&chunk, make_send_inst(1, msg_minus));     // [13] n - 1
    write_chunk(&chunk, make_inst(OP_SET_LOCAL, 1));       // [14] n に格納
    write_chunk(&chunk, make_inst(OP_POP, 0));             // [15] 代入式の余剰値を破棄

    // [16] ループ先頭 [2] へ戻る (オフセットは 17 - 2 = 15)
    write_chunk(&chunk, make_inst(OP_LOOP, 15));

    // [Index: 17] --- ループ脱出先 ---
    // sum を読み出して返す
    write_chunk(&chunk, make_inst(OP_GET_LOCAL, 0));       // [17]
    write_chunk(&chunk, make_inst(OP_RETURN, 0));          // [18]

    // 実行
    InterpretResult result = interpret(vm, &chunk);

    free_chunk(&chunk);
    free_vm(vm);

    return (result == INTERPRET_OK) ? 0 : 1;
}

static bool test_call_frame(VM* vm)
{
    // ---------------------------------------------
    // 1. 子関数: add(a, b) -> a + b
    // ---------------------------------------------
    Chunk add_chunk;
    init_chunk(&add_chunk);

    // slots[0] (a) を取得
    write_chunk(&add_chunk, make_inst(OP_GET_LOCAL, 0));
    // slots[1] (b) を取得
    write_chunk(&add_chunk, make_inst(OP_GET_LOCAL, 1));
    // + メッセージを送信（引数1個、メッセージインデックスは既存の "+"）
    write_chunk(&add_chunk, make_send_inst(1, add_constant(&add_chunk, make_obj((void*)"+")))); 
    // 結果を呼び出し元に返す
    write_chunk(&add_chunk, make_inst(OP_RETURN, 0));

    // ---------------------------------------------
    // 2. メイン処理: add(10, 20) を呼ぶ
    // ---------------------------------------------
    Chunk main_chunk;
    init_chunk(&main_chunk);

    // 引数をスタックに積む
    write_chunk(&main_chunk, make_inst(OP_CONSTANT, add_constant(&main_chunk, make_int(10))));
    write_chunk(&main_chunk, make_inst(OP_CONSTANT, add_constant(&main_chunk, make_int(20))));

    // 呼び出す関数のChunkポインタをスタックに積む
    write_chunk(&main_chunk, make_inst(OP_CONSTANT, add_constant(&main_chunk, make_obj(&add_chunk))));

    // 引数2個で呼び出し（OP_CALL 2）
    write_chunk(&main_chunk, make_inst(OP_CALL, 2));

    // 戻ってきたらプログラム終了
    write_chunk(&main_chunk, make_inst(OP_RETURN, 0));

    // ---------------------------------------------
    // 3. 実行
    // ---------------------------------------------
    InterpretResult result = interpret(vm, &main_chunk);

    free_chunk(&main_chunk);
    free_chunk(&add_chunk);
    free_vm(vm);

    return (result == INTERPRET_OK) ? 0 : 1;
}

int main(void) {
    VM vm;
    init_vm(&vm);

    return test_call_frame(&vm);
}
