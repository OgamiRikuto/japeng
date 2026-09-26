#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <libgen.h>
#include <time.h>
#include "defs.h"
#include "common.h"
#include "parse.h"
#include "compiler.h"
#include "vm.h"

int parsed_file_count = 0;

static void print_usage(const char* prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s <file.je> [class.cd ...]     (指定ファイルのみ実行)\n", prog);
    fprintf(stderr, "  %s -d <dir_path>                (ディレクトリ全探索実行)\n", prog);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    
    init_symbols();

    // -d オプションの判定
    if (strcmp(argv[1], "-d") == 0 || strcmp(argv[1], "--dir") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: -d requires directory or file path.\n");
            return EXIT_FAILURE;
        }

        char* path_copy = strdup(argv[2]);
        char* target_dir = path_copy;

        // .je や .cd が渡されたら親ディレクトリを取得
        if (strstr(argv[2], ".je") || strstr(argv[2], ".cd")) {
            target_dir = dirname(path_copy);
        }

        parse(target_dir);
        free(path_copy);
    } else {
        // 通常モード: 渡されたファイルのみを個別にパース
        for (int i = 1; i < argc; i++) {
            parse_file(argv[i]);
        }
    }

    if (parsed_je == NULL) {
        fprintf(stderr, "Error: No main script (.je) provided or parsed.\n");
        return EXIT_FAILURE;
    }

    if (syntax_error_count > 0) {
        fprintf(stderr, "\033[1;31m%d syntax error(s) generated.\033[0m\n", syntax_error_count);
        exit(EXIT_FAILURE);
    }

    VM vm;
    init_vm(&vm);

    Chunk main_chunk;
    init_chunk(&main_chunk);

    Compiler compiler;
    init_compiler(&compiler, &main_chunk);

    // 【パス 1】クラスの骨格登録
    for (int i = 0; i < parsed_cd_count; i++) {
        declare_class_skeleton(&compiler, parsed_cd[i]);
    }
    
    // 【パス 2】全クラスの中身を解決し、フィールド配置とメソッドを生成
    for (int i = 0; i < parsed_cd_count; i++) {
        compile_class_body(&compiler, parsed_cd[i]);
    }
    
    // 【動作スクリプト】メインエントリ (.je) をコンパイル
    compile(&compiler, parsed_je);
    
    // スクリプト末尾の安全終了
    emit_constant(&compiler, make_nil());
    emit_inst(&compiler, OP_RETURN, 0);

    write_chunk(&main_chunk, make_inst(OP_RETURN, 0));

    // VM 実行
    clock_t start = clock();
    InterpretResult result = interpret(&vm, &main_chunk);
    clock_t end = clock();

    printf("Pure VM Time: %f ms\n", (double)(end - start) / CLOCKS_PER_SEC * 1000.0);

    // 後始末
    free_chunk(&main_chunk);
    free_vm(&vm);
    free_symbol_pool();

    if (result != INTERPRET_OK) {
        fprintf(stderr, "Execution failed with runtime error.\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
