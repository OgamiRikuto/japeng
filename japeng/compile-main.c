#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <getopt.h>
#include <dirent.h>
#include <sys/stat.h>
#include "defs.h"
#include "common.h"
#include "parse.h"
#include "compiler.h"
#include "bcd_writer.h"

static void get_basename_without_ext(const char* filepath, char* out, size_t max_len) {
    const char* slash = strrchr(filepath, '/');
    const char* fname = slash ? slash + 1 : filepath;

    strncpy(out, fname, max_len - 1);
    out[max_len - 1] = '\0';

    char* dot = strrchr(out, '.');
    if (dot) *dot = '\0'; // .je や .jje を削る
}

static void print_usage(const char* prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s [-ja] <file.je|file.cd ...>         (指定ファイルのコンパイル)\n", prog);
    fprintf(stderr, "  %s [-ja] -d <dir_path>                 (ディレクトリ全探索コンパイル)\n", prog);
}

// =============================================================================
// ディレクトリ再帰削除ヘルパー
// =============================================================================
static bool remove_dir_recursive(const char* path) {
    DIR* d = opendir(path);
    if (!d) return false;

    struct dirent* p;
    char sub_path[1024];

    while ((p = readdir(d)) != NULL) {
        if (strcmp(p->d_name, ".") == 0 || strcmp(p->d_name, "..") == 0) {
            continue;
        }

        snprintf(sub_path, sizeof(sub_path), "%s/%s", path, p->d_name);
        struct stat st;
        if (stat(sub_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                remove_dir_recursive(sub_path);
            } else {
                unlink(sub_path);
            }
        }
    }
    closedir(d);
    return (rmdir(path) == 0);
}

// =============================================================================
// キャッシュクリーン処理
// =============================================================================
static int handle_clean_command(int argc, char** argv) {
    const char* target = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--clean") == 0 || strcmp(argv[i], "-clean") == 0 ||
            strcmp(argv[i], "--初期化") == 0) {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                target = argv[i + 1];
            }
            break;
        }
    }

    bool clean_all = (target != NULL && (strcmp(target, "all") == 0 || strcmp(target, "全") == 0));
    bool removed_any = false;
    char cache_path[1024];
    struct stat st;

    // 1. 標準ライブラリのキャッシュ削除 (all 指定時)
    if (clean_all) {
        // ★ 場所探索関数を使って lib の真の位置を取得！
        const char* lib_dir = get_japeng_lib_dir();
        snprintf(cache_path, sizeof(cache_path), "%s/.je_cache", lib_dir);

        if (stat(cache_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (remove_dir_recursive(cache_path)) {
                printf("ライブラリキャッシュを削除しました: %s\n", cache_path);
                removed_any = true;
            }
        }
    }

    // 2. プロジェクト（指定パス、またはカレント）のキャッシュ削除
    const char* proj_dir = (!target || clean_all) ? "." : target;
    snprintf(cache_path, sizeof(cache_path), "%s/.je_cache", proj_dir);

    if (stat(cache_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        if (remove_dir_recursive(cache_path)) {
            printf("プロジェクトキャッシュを削除しました: %s\n", cache_path);
            removed_any = true;
        }
    }

    if (!removed_any) {
        printf("削除対象のキャッシュは見つかりませんでした。\n");
    }

    return 0;
}

// ★ AST_STMT で複数クラスが繋がっていても安全に再帰して差分チェックを行うヘルパー
static void collect_freshness_targets(ASTNode* node, const char* project_dir, ASTNode** targets, int* target_count) {
    if (!node) return;

    if (node->kind == AST_STMT) {
        collect_freshness_targets(node->stmt.left, project_dir, targets, target_count);
        collect_freshness_targets(node->stmt.right, project_dir, targets, target_count);
        return;
    }

    if (node->kind == AST_CLASS_DEF) {
        ASTNode* cnode = node->class_def.class;
        if (cnode && cnode->kind == AST_CLASS && cnode->class.classname) {
            Symbol* cname = cnode->class.classname->identifier.name;
            char bcd_path[1024];
            get_cache_filepath(project_dir, cname->chars, "bcd", bcd_path, sizeof(bcd_path));

            // .bcd が存在しない、または .cd の方が新しい場合のみ再コンパイル対象へ
            if (!is_cache_fresh(bcd_path, project_dir)) {
                targets[(*target_count)++] = node;
            }
        }
    }
}

int main(int argc, char** argv) {
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--clean") == 0) {
                return handle_clean_command(argc, argv);
            }
        }
    }
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

    init_symbols();
    // ★ builtins.cd の直接パースは完全撤廃（resolve_class の自動ロードに一本化）

    char project_dir_buf[1024] = ".";
    const char* project_dir = project_dir_buf;

    // 1. 入力パース
    if (target_dir != NULL) {
        char* path_copy = strdup(target_dir);
        if (strstr(target_dir, ".je") || strstr(target_dir, ".cd") ||
            strstr(target_dir, ".jje") || strstr(target_dir, ".jcd")) {
            char* d = dirname(path_copy);
            strncpy(project_dir_buf, d, sizeof(project_dir_buf) - 1);
        } else {
            strncpy(project_dir_buf, target_dir, sizeof(project_dir_buf) - 1);
        }
        free(path_copy);
        parse(project_dir_buf);
    } else {
        if (optind >= argc) {
            fprintf(stderr, (current_lang == LANG_JA) 
                ? "エラー: コンパイル対象ファイルが指定されていません。\n"
                : "Error: No target files provided.\n");
            return EXIT_FAILURE;
        }

        char* path_copy = strdup(argv[optind]);
        char* d = dirname(path_copy);
        strncpy(project_dir_buf, d, sizeof(project_dir_buf) - 1);
        free(path_copy);

        for (int i = optind; i < argc; i++) {
            parse_file(argv[i]);
        }
    }

    if (syntax_error_count > 0) {
        fprintf(stderr, (current_lang == LANG_JA)
            ? "\033[1;31m%d 個の構文エラーが検出されました。\033[0m\n"
            : "\033[1;31m%d syntax error(s) generated.\033[0m\n", syntax_error_count);
        return EXIT_FAILURE;
    }

    // 2. クラス定義の差分チェック & 4パスコンパイル
    if (parsed_cd_count > 0) {
        ASTNode* compile_targets[MAX_FILES];
        int target_count = 0;

        // ★ AST_STMT に対応した安全な再帰で再コンパイル対象を抽出
        for (int i = 0; i < parsed_cd_count; i++) {
            collect_freshness_targets(parsed_cd[i], project_dir, compile_targets, &target_count);
        }

        // 更新が必要なクラスがある場合のみコンパイルを実行
        if (target_count > 0) {
            Chunk dummy_chunk;
            init_chunk(&dummy_chunk);

            Compiler cd_compiler;
            init_compiler(&cd_compiler, &dummy_chunk);
            cd_compiler.output_dir = project_dir;

            // 型解決のため、パースされた全クラスの骨格・関係性・シグネチャをメモリに展開
            for (int i = 0; i < parsed_cd_count; i++) declare_class_skeleton(&cd_compiler, parsed_cd[i]);
            for (int i = 0; i < parsed_cd_count; i++) resolve_class_skeleton_relations(&cd_compiler, parsed_cd[i]);
            for (int i = 0; i < parsed_cd_count; i++) declare_class_signatures(&cd_compiler, parsed_cd[i]);

            // ★ 更新が必要なクラスだけをコンパイルして .bcd を書き出し
            for (int i = 0; i < target_count; i++) {
                compile_class_body(&cd_compiler, compile_targets[i]);
            }

            free_chunk(&dummy_chunk);
        }
    }

    // 3. スクリプトのコンパイル (.bje 出力)
    for (int i = 0; i < parsed_je_count; i++) {
        Chunk script_chunk;
        init_chunk(&script_chunk);

        Compiler script_compiler;
        init_compiler(&script_compiler, &script_chunk);
        script_compiler.output_dir = project_dir;

        compile(&script_compiler, parsed_je[i]);
        emit_constant(&script_compiler, make_int(0));
        emit_inst(&script_compiler, OP_RETURN, 1);

        char script_name[256];
        get_basename_without_ext(parsed_je[i]->loc.filename, script_name, sizeof(script_name));
        write_script_bje(script_compiler.output_dir, script_name, &script_chunk);

        free_chunk(&script_chunk);
    }

    free_symbol_pool();

    if (had_compile_error) {
        fprintf(stderr, (current_lang == LANG_JA)
            ? "\033[1;31m%d 個のコンパイルエラーが検出されました。\033[0m\n"
            : "\033[1;31m%d compile error(s) generated.\033[0m\n", compile_error_count);
        return EXIT_FAILURE;
    }

    printf((current_lang == LANG_JA)
        ? "コンパイル完了: '%s/.je_cache/' にバイナリを出力しました。\n"
        : "Compilation finished: Binaries generated in '%s/.je_cache/'.\n", project_dir);

    return EXIT_SUCCESS;
}
