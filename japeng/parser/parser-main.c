#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include "defs.h"
#include "parse.h"

int parsed_file_count = 0;

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <target_file.je>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char* path_copy = strdup(argv[1]);
    char* base_dir = dirname(path_copy);

    printf("--- Compiling Project: %s ---\n", base_dir);

    parse(base_dir);

    free(path_copy);

    if (syntax_error_count > 0) {
        fprintf(stderr, "\033[1;31m%d syntax error(s) generated.\033[0m\n", syntax_error_count);
        return EXIT_FAILURE;
    }

    printf("\n=== AST PRINT (%d files) ===\n", parsed_cd_count + 1);
    for (int i = 0; i < parsed_cd_count; i++) {
        printf("\n--- %s ---\n", parsed_cd[i]->filename);
        print_ast(parsed_cd[i], 0);
    }
    printf("\n--- %s ---\n", parsed_je->filename);
    print_ast(parsed_je, 0);
    
    return EXIT_SUCCESS;
}
