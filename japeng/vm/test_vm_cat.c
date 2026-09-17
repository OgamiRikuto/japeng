#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "chunk.h"
#include "object.h"
#include "class.h"
#include "symbol.h"
#include "opcode.h"
#include "vm.h"

// ==========================================
// 1. run メソッドの構築 (二重再帰フィボナッチ)
// ==========================================
static ObjFunction* build_run_method1(void) {
    // チャンクをヒープに確保して初期化
    Chunk* c = (Chunk*)malloc(sizeof(Chunk));
    init_chunk(c);

    // new_function は (Chunk*, uint8_t arity) の2引数
    ObjFunction* fn = new_function(c, 1); // 引数1: n (slot 1) / slot 0 は self

    // 定数登録
    uint32_t c_2 = add_constant(c, make_int(2));
    uint32_t c_1 = add_constant(c, make_int(1));

    // メッセージ名シンボル
    uint16_t m_gt  = (uint16_t)add_constant(c, make_obj((Obj*)intern_cstr(">")));
    uint16_t m_sub = (uint16_t)add_constant(c, make_obj((Obj*)intern_cstr("-")));
    uint16_t m_add = (uint16_t)add_constant(c, make_obj((Obj*)intern_cstr("+")));
    uint16_t m_run = (uint16_t)add_constant(c, make_obj((Obj*)intern_cstr("run")));

    // --- [0〜2] 判定: 2 > n ---
    write_chunk(c, make_inst(OP_CONSTANT, c_2));            // 2
    write_chunk(c, make_inst(OP_GET_LOCAL, 1));            // n
    write_chunk(c, make_send_inst(1, m_gt));               // 2 > n (dispatch_integer)

    // --- [3] 分岐: 偽なら Then節（2命令）をスキップ ---
    write_chunk(c, make_inst(OP_JUMP_IF_FALSE, 2));

    // --- [4〜5] Then 節: ret n ---
    write_chunk(c, make_inst(OP_GET_LOCAL, 1));            // n
    write_chunk(c, make_inst(OP_RETURN, 0));

    // --- [6〜10] Else 節: 項1 = self run (n - 1) ---
    write_chunk(c, make_inst(OP_GET_LOCAL, 0));            // self
    write_chunk(c, make_inst(OP_GET_LOCAL, 1));            // n
    write_chunk(c, make_inst(OP_CONSTANT, c_1));            // 1
    write_chunk(c, make_send_inst(1, m_sub));              // n - 1
    write_chunk(c, make_send_inst(1, m_run));              // self run (n - 1)

    // --- [11〜15] Else 節: 項2 = self run (n - 2) ---
    write_chunk(c, make_inst(OP_GET_LOCAL, 0));            // self
    write_chunk(c, make_inst(OP_GET_LOCAL, 1));            // n
    write_chunk(c, make_inst(OP_CONSTANT, c_2));            // 2
    write_chunk(c, make_send_inst(1, m_sub));              // n - 2
    write_chunk(c, make_send_inst(1, m_run));              // self run (n - 2)

    // --- [16〜17] 項1 + 項2 を計算して復帰 ---
    write_chunk(c, make_send_inst(1, m_add));              // 項1 + 項2
    write_chunk(c, make_inst(OP_RETURN, 0));

    return fn;
}

static ObjFunction* build_run_method2(void) {
    Chunk* c = (Chunk*)malloc(sizeof(Chunk));
    init_chunk(c);

    // 引数3個: n (slot 1), a (slot 2), b (slot 3) / slot 0 は self
    ObjFunction* fn = new_function(c, 3);

    // 定数
    uint32_t c_1   = add_constant(c, make_int(1));
    uint16_t m_gt  = (uint16_t)add_constant(c, make_obj((Obj*)intern_cstr(">")));
    uint16_t m_sub = (uint16_t)add_constant(c, make_obj((Obj*)intern_cstr("-")));
    uint16_t m_add = (uint16_t)add_constant(c, make_obj((Obj*)intern_cstr("+")));
    uint16_t m_run = (uint16_t)add_constant(c, make_obj((Obj*)intern_cstr("run")));

    // --- [0〜2] 判定: 1 > n (n が 0 以下なら終了) ---
    write_chunk(c, make_inst(OP_CONSTANT, c_1));            // 1
    write_chunk(c, make_inst(OP_GET_LOCAL, 1));            // n
    write_chunk(c, make_send_inst(1, m_gt));               // 1 > n

    // --- [3] 分岐: 偽なら Then節（2命令）をスキップ ---
    write_chunk(c, make_inst(OP_JUMP_IF_FALSE, 2));

    // --- [4〜5] Then 節: return a ---
    write_chunk(c, make_inst(OP_GET_LOCAL, 2));            // a
    write_chunk(c, make_inst(OP_RETURN, 0));

    // --- [6〜15] Else 節 (末尾呼び出し): self run (n - 1) b (a + b) ---
    write_chunk(c, make_inst(OP_GET_LOCAL, 0));            // [6]  レシーバ: self

    // 第1引数: n - 1
    write_chunk(c, make_inst(OP_GET_LOCAL, 1));            // [7]  n
    write_chunk(c, make_inst(OP_CONSTANT, c_1));            // [8]  1
    write_chunk(c, make_send_inst(1, m_sub));              // [9]  n - 1

    // 第2引数: b
    write_chunk(c, make_inst(OP_GET_LOCAL, 3));            // [10] b

    // 第3引数: a + b
    write_chunk(c, make_inst(OP_GET_LOCAL, 2));            // [11] a
    write_chunk(c, make_inst(OP_GET_LOCAL, 3));            // [12] b
    write_chunk(c, make_send_inst(1, m_add));              // [13] a + b

    // 末尾再帰呼び出し (引数3個)
    write_chunk(c, make_send_inst(3, m_run));              // [14] self run (n-1) b (a+b)
    write_chunk(c, make_inst(OP_RETURN, 0));               // [15]

    return fn;
}
static ObjFunction* build_run_method3(void) {
    Chunk* c = (Chunk*)malloc(sizeof(Chunk));
    init_chunk(c);

    // 引数1個: n (slot 1) / slot 0 は self
    ObjFunction* fn = new_function(c, 1);

    // 定数登録
    uint32_t c_0   = add_constant(c, make_int(0));
    uint32_t c_1   = add_constant(c, make_int(1));
    uint16_t m_gt  = (uint16_t)add_constant(c, make_obj((Obj*)intern_cstr(">")));
    uint16_t m_sub = (uint16_t)add_constant(c, make_obj((Obj*)intern_cstr("-")));
    uint16_t m_add = (uint16_t)add_constant(c, make_obj((Obj*)intern_cstr("+")));

    // --- [0〜1] ローカル変数初期化: a = 0 (slot 2), b = 1 (slot 3) ---
    write_chunk(c, make_inst(OP_CONSTANT, c_0));            // [0] a = 0
    write_chunk(c, make_inst(OP_CONSTANT, c_1));            // [1] b = 1

    // --- [2〜5] ループ判定: 1 > n (target: [2]) ---
    write_chunk(c, make_inst(OP_CONSTANT, c_1));            // [2] 1
    write_chunk(c, make_inst(OP_GET_LOCAL, 1));            // [3] n
    write_chunk(c, make_send_inst(1, m_gt));               // [4] 1 > n
    // 偽(n >= 1: ループ続行)なら脱出節([6][7])をスキップして [8] へジャンプ
    write_chunk(c, make_inst(OP_JUMP_IF_FALSE, 2));        // [5] offset: 2

    // --- [6〜7] ループ脱出節: return a ---
    write_chunk(c, make_inst(OP_GET_LOCAL, 2));            // [6] a
    write_chunk(c, make_inst(OP_RETURN, 0));               // [7] return a

    // --- [8〜15] ループ本体: temp = a + b, a = b, b = temp ---
    write_chunk(c, make_inst(OP_GET_LOCAL, 2));            // [8]  a
    write_chunk(c, make_inst(OP_GET_LOCAL, 3));            // [9]  b
    write_chunk(c, make_send_inst(1, m_add));              // [10] a + b (スタックトップに保持)
    write_chunk(c, make_inst(OP_GET_LOCAL, 3));            // [11] b
    write_chunk(c, make_inst(OP_SET_LOCAL, 2));            // [12] a = b
    write_chunk(c, make_inst(OP_POP, 0));                  // [13] 評価値を破棄
    write_chunk(c, make_inst(OP_SET_LOCAL, 3));            // [14] b = temp (a+b)
    write_chunk(c, make_inst(OP_POP, 0));                  // [15] 評価値を破棄

    // --- [16〜20] n = n - 1 ---
    write_chunk(c, make_inst(OP_GET_LOCAL, 1));            // [16] n
    write_chunk(c, make_inst(OP_CONSTANT, c_1));            // [17] 1
    write_chunk(c, make_send_inst(1, m_sub));              // [18] n - 1
    write_chunk(c, make_inst(OP_SET_LOCAL, 1));            // [19] n = n - 1
    write_chunk(c, make_inst(OP_POP, 0));                  // [20] 評価値を破棄

    // --- [21] ループ先頭 [2] へ巻き戻る (22 - 20 = 2) ---
    write_chunk(c, make_inst(OP_LOOP, 20));                // [21] offset: 20

    return fn;
}
// ==========================================
// 2. Fibo クラスの構築
// ==========================================


static ObjClass* build_fibo_class(void) {
    ObjString* class_name = intern_cstr("Fibo");

    // new_class は (name, type, superclass, superclass_type, delegate_count) の5引数
    ObjClass* klass = new_class(class_name, NULL, NULL, NULL, 0);

    // メソッドの登録
    ObjFunction* run_fn = build_run_method3();
    add_method(klass, intern_cstr("run"), make_obj((Obj*)run_fn));

    return klass;
}

// ==========================================
// 3. メインスクリプト Chunk の構築
// ==========================================
static Chunk* build_main_chunk(ObjClass* klass) {
    Chunk* c = (Chunk*)malloc(sizeof(Chunk));
    init_chunk(c);

    // クラス実体と数値を定数テーブルに登録
    uint32_t c_klass = add_constant(c, make_obj((Obj*)klass));
    uint32_t c_25    = add_constant(c, make_int(25));
    uint32_t c_1    = add_constant(c, make_int(1));
    uint32_t c_0    = add_constant(c, make_int(0));
    uint16_t m_new   = (uint16_t)add_constant(c, make_obj((Obj*)intern_cstr("new")));
    uint16_t m_run   = (uint16_t)add_constant(c, make_obj((Obj*)intern_cstr("run")));

    // app = Fibo new (クラスを定数からスタックへ積んで new を送信)
    write_chunk(c, make_inst(OP_CONSTANT, c_klass));       // レシーバ: Fiboクラス
    write_chunk(c, make_send_inst(0, m_new));              // new 呼び出し
    write_chunk(c, make_inst(OP_SET_LOCAL, 0));            // slot 0 に格納

    // ans = app run 10
    write_chunk(c, make_inst(OP_GET_LOCAL, 0));            // レシーバ: app
    write_chunk(c, make_inst(OP_CONSTANT, c_25));           // 引数: 10
    write_chunk(c, make_send_inst(1, m_run));              // run 呼び出し

    // write_chunk(c, make_inst(OP_GET_LOCAL, 0)); // app
    // write_chunk(c, make_inst(OP_CONSTANT, c_25)); // 25
    // write_chunk(c, make_inst(OP_CONSTANT, c_0));  // 0
    // write_chunk(c, make_inst(OP_CONSTANT, c_1));  // 1
    // write_chunk(c, make_send_inst(3, m_run));     // app run 25 0 1 (引数3個)

    // 計算結果がスタックトップにある状態で return
    write_chunk(c, make_inst(OP_RETURN, 0));

    return c;
}

// ==========================================
// 4. テスト実行
// ==========================================
int main(void) {
    VM vm;
    init_vm(&vm);
    // ObjString* test_sym = intern_cstr(">");
    // printf("[CHECK] sym: %p, len: %d, str: '%s'\n", 
    //        (void*)test_sym, test_sym->length, test_sym->chars);

    extern ObjString* sym_new;
    sym_new = intern_cstr("new");
    // クラス生成
    ObjClass* fibo_class = build_fibo_class();

    // メインチャンク生成
    Chunk* main_chunk = build_main_chunk(fibo_class);

    printf("=== Starting JapEng VM Fibonacci Execution ===\n");
    clock_t start = clock();
    interpret(&vm, main_chunk);
    clock_t end = clock();

    printf("Pure VM Time: %f ms\n", (double)(end - start) / CLOCKS_PER_SEC * 1000.0);

    free_chunk(main_chunk);
    free(main_chunk);
    free_class(fibo_class);
    free_vm(&vm);

    return 0;
}
