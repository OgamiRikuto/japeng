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

    printf("\n=== AST PRINT (%d files) ===\n", parsed_file_count);
    for (int i = 0; i < parsed_file_count; i++) {
        printf("\n--- %s ---\n", parsed_files[i]->filename);
        print_ast(parsed_files[i], 0);
    }
    
    return EXIT_SUCCESS;
}
