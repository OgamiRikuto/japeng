#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <time.h>
#include <getopt.h>
#include <sys/stat.h>

#include "common.h"
#include "vm.h"
#include "bje_loader.h"
#include "bcd_class_reader.h"

static void print_usage(const char* prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s [-ja] <script.je | script.jje | script.bje>   (実行)\n", prog);
    fprintf(stderr, "  %s [-ja] -d <dir_path>                          (ディレクトリ内 main 実行)\n", prog);
}

static void extract_project_and_script(const char* target, char* out_dir, size_t dir_len, char* out_script, size_t script_len) {
    char* path_copy1 = strdup(target);
    char* path_copy2 = strdup(target);

    // ディレクトリ特定
    char* dname = dirname(path_copy1);
    // もし .je_cache ディレクトリ自身が指定された場合はその親を取る
    char* cache_sub = strstr(dname, "/.je_cache");
    if (cache_sub != NULL) {
        *cache_sub = '\0';
    }
    strncpy(out_dir, dname, dir_len - 1);
    out_dir[dir_len - 1] = '\0';

    // スクリプト名特定
    char* bname = basename(path_copy2);
    strncpy(out_script, bname, script_len - 1);
    out_script[script_len - 1] = '\0';

    char* dot = strrchr(out_script, '.');
    if (dot != NULL) {
        *dot = '\0'; // .je, .jje, .bje を削る
    }

    free(path_copy1);
    free(path_copy2);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    char* target_dir = NULL;

    static struct option long_options[] = {
        {"ja",   no_argument,       0, 'j'},
        {"dir",  required_argument, 0, 'd'},
        {"help", no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
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

    char project_dir[1024] = ".";
    char script_name[256] = "main";

    if (target_dir != NULL) {
        struct stat st;
        if (stat(target_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
            strncpy(project_dir, target_dir, sizeof(project_dir) - 1);
            strcpy(script_name, "main");
        } else {
            extract_project_and_script(target_dir, project_dir, sizeof(project_dir), script_name, sizeof(script_name));
        }
    } else {
        if (optind >= argc) {
            fprintf(stderr, (current_lang == LANG_JA) 
                ? "エラー: 実行対象スクリプトが指定されていません。\n"
                : "Error: No target script provided.\n");
            return EXIT_FAILURE;
        }
        extract_project_and_script(argv[optind], project_dir, sizeof(project_dir), script_name, sizeof(script_name));
    }

    init_symbols();

    VM vm;
    init_vm(&vm);
    vm.project_dir = project_dir;

    Chunk main_chunk;
    if (!load_script_bje(&vm, project_dir, script_name, &main_chunk)) {
        fprintf(stderr, (current_lang == LANG_JA)
            ? "エラー: バイナリ '%s/.je_cache/%s.bje' が見つかりません。\n先に 'jpenc' でコンパイルしてください。\n"
            : "Error: Binary '%s/.je_cache/%s.bje' not found.\nPlease compile with 'jpenc' first.\n",
            project_dir, script_name);
        free_vm(&vm);
        free_symbol_pool();
        return EXIT_FAILURE;
    }

    clock_t start = clock();
    InterpretResult result = interpret(&vm, &main_chunk);
    clock_t end = clock();
    printf("Pure VM Time: %f ms\n", (double)(end - start) / CLOCKS_PER_SEC * 1000.0);

    int exit_code = 0;
    if (result == INTERPRET_OK && vm.stack_top > vm.stack) {
        Value exit_val = pop(&vm);
        if (is_int(exit_val)) {
            exit_code = as_int(exit_val);
        }
    }

    free_chunk(&main_chunk);
    free_vm(&vm);
    free_symbol_pool();

    if (result != INTERPRET_OK) {
        return EXIT_FAILURE;
    }

    return exit_code;
}
