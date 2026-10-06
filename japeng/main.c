#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <libgen.h>
#include <time.h>
#include <getopt.h>
#include "defs.h"
#include "common.h"
#include "parse.h"
#include "compiler.h"
#include "vm.h"

int parsed_file_count = 0;

static void print_usage(const char* prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s [-ja] <file.je> [class.cd ...]     (指定ファイルのみ実行)\n", prog);
    fprintf(stderr, "  %s [-ja] -d <dir_path>                (ディレクトリ全探索実行)\n", prog);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    char* target_dir = NULL;

    // オプション定義 (-ja, -d / --dir, -h / --help)
    static struct option long_options[] = {
        {"ja",   no_argument,       0, 'j'},
        {"dir",  required_argument, 0, 'd'},
        {"help", no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    // "d:" は -d が引数を取ることを表す
    while ((opt = getopt_long_only(argc, argv, "d:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'j':
                current_lang = LANG_JA;
                break;
            case 'd':
                target_dir = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }
    
    init_symbols();

    // -d オプションの判定
    if (target_dir != NULL) {
        // -d モード
        char* path_copy = strdup(target_dir);
        char* dir_to_parse = path_copy;

        if (strstr(target_dir, ".je") || strstr(target_dir, ".cd")) {
            dir_to_parse = dirname(path_copy);
        }

        parse(dir_to_parse);
        free(path_copy);
    } else {
        // ファイル直接指定モード (optind 以降に残りのファイル名がまとまっている)
        if (optind >= argc) {
            fprintf(stderr, (current_lang == LANG_JA) 
                ? "エラー: 実行するスクリプト (.je) が指定されていません。\n"
                : "Error: No main script (.je) provided.\n");
            return EXIT_FAILURE;
        }

        for (int i = optind; i < argc; i++) {
            parse_file(argv[i]);
        }
    }

    if (parsed_je == NULL) {
        fprintf(stderr, (current_lang == LANG_JA)
            ? "エラー: メインソースコード(.je) が解析されていません。\n"
            : "Error: No main script (.je) provided or parsed.\n");
        return EXIT_FAILURE;
    }

    if (syntax_error_count > 0) {
        fprintf(stderr, (current_lang == LANG_JA)
            ? "\033[1;31m%d 個の構文エラーが検出されました。\033[0m\n"
            : "\033[1;31m%d syntax error(s) generated.\033[0m\n", syntax_error_count);
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
        fprintf(stderr, (current_lang == LANG_JA)
            ? "実行時エラーにより、実行が失敗しました。\n"
            : "Execution failed with runtime error.\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
