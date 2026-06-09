#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "ast.h"
#include "defs.h"
#include "parse.h"

extern FILE* yyin;
const char* current_filename = "";

ASTNode* parsed_files[MAX_FILES];

static int is_target_file(const char* filename) {
    const char* dot = strrchr(filename, '.');
    if(dot == NULL) return 0;
    return (strcmp(dot, ".je") == 0 || strcmp(dot, ".cd") == 0);
}

void parse(const char* dir_path)
{
    DIR* dir = opendir(dir_path);
    if(dir == NULL) return;

    struct dirent* entry;
    while((entry = readdir(dir)) != NULL) {
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat path_stat;
        if(stat(full_path, &path_stat) != 0) continue;

        if(S_ISDIR(path_stat.st_mode)) parse(full_path);
        else if(S_ISREG(path_stat.st_mode)) {
            if(is_target_file(entry->d_name)) {
                printf("Processing: %s\n", full_path);

                FILE* file = fopen(full_path, "r");
                if(file) {
                    yyin = file;
                    current_filename = strdup(full_path);

                    extern int linecounter;
                    extern int yylineno;
                    void yyrestart(FILE *input_file); // Flexの内部バッファリセット関数
                    
                    current_filename = strdup(full_path); 
                    
                    // Lexerに新しいファイルを渡し、内部状態を完全に初期化する
                    yyrestart(file);
                    
                    // 行番号を1にリセットする
                    linecounter = 1;
                    yylineno = 1;

                    yyparse();

                    fclose(file);
                } 
            }
        }
    }
    closedir(dir);
}

void print_ast(ASTNode* node, int depth)
{
    if (!node) return;

    // ツリーの枝（インデント）を描画
    for (int i = 0; i < depth; i++) {
        printf("  | ");
    }
    printf("└─ ");

    // ノードの種類ごとに表示を分岐
    switch (node->kind) {
        case AST_STMT:
            printf("[STMT]\n");
            print_ast(node->stmt.left, depth + 1);
            print_ast(node->stmt.right, depth + 1);
            break;
            
        case AST_CLASS_DEF:
            printf("[CLASS_DEF]\n");
            print_ast(node->class_def.class, depth + 1);
            if (node->class_def.super_class) {
                for (int i = 0; i <= depth; i++) printf("  | ");
                printf(" [SUPER]\n");
                print_ast(node->class_def.super_class, depth + 2);
            }
            print_ast(node->class_def.members, depth + 1);
            break;
            
        case AST_FIELD_DECL:
            printf("[FIELD_DECL] (static: %s)\n", node->field_decl.is_static ? "true" : "false");
            print_ast(node->field_decl.identifier, depth + 1);
            print_ast(node->field_decl.type, depth + 1);
            if(node->field_decl.default_data) {
                for (int i = 0; i <= depth; i++) printf("  | ");
                printf(" [DEFAULT]\n");
                print_ast(node->field_decl.default_data, depth + 2);
            }
            break;
            
        case AST_VAR_DECL:
            printf("[VAR_DECL]\n");
            print_ast(node->var_decl.identifier, depth + 1);
            print_ast(node->var_decl.type, depth + 1);
            break;

        case AST_CLASS:
            printf("[CLASS_TYPE]\n");
            print_ast(node->class.classname, depth + 1);
            if (node->class.types) print_ast(node->class.types, depth + 1); // Generics等
            break;
            
        case AST_IDENTIFIER:
            printf("[ID: %s]\n", node->identifier.name);
            break;
            
        case AST_LITERAL:
            // 今回はシンプルに値のポインタ/生データを16進数で表示 (NaN Boxing対応)
            printf("[LITERAL] (Value: %llx)\n", (unsigned long long)node->literal.value);
            break;
            
        case AST_SEND:
            printf("[SEND]\n");
            if (node->send.receiver) {
                for (int i = 0; i <= depth; i++) printf("  | ");
                printf(" [RECEIVER]\n");
                print_ast(node->send.receiver, depth + 2);
            }
            for (int i = 0; i <= depth; i++) printf("  | ");
            printf(" [MESSAGE]\n");
            print_ast(node->send.message, depth + 2);
            
            if (node->send.args) {
                for (int i = 0; i <= depth; i++) printf("  | ");
                printf(" [ARGS]\n");
                print_ast(node->send.args, depth + 2);
            }
            break;
            
        case AST_BLOCK:
            printf("[BLOCK]\n");
            if (node->block.args) {
                for (int i = 0; i <= depth; i++) printf("  | ");
                printf(" [BLOCK_ARGS]\n");
                print_ast(node->block.args, depth + 2);
            }
            if (node->block.rets) {
                for (int i = 0; i <= depth; i++) printf("  | ");
                printf(" [BLOCK_RET]\n");
                print_ast(node->block.rets, depth + 2);
            }
            for (int i = 0; i <= depth; i++) printf("  | ");
            printf(" [BLOCK_BODY]\n");
            print_ast(node->block.body, depth + 2);
            break;
            
        case AST_RETURN:
            printf("[RETURN]\n");
            print_ast(node->ret.expr, depth + 1);
            break;
            
        case AST_BREAK:
            printf("[BREAK]\n");
            break;
            
        case AST_CONTINUE:
            printf("[CONTINUE]\n");
            break;
            
        default:
            printf("[UNKNOWN NODE KIND: %d]\n", node->kind);
            break;
    }
}

void dump_ast(ASTNode* node, int depth)
{
    if (!node) return;

    // インデントの生成
    for (int i = 0; i < depth; i++) printf("  ");

    // ノードの共通属性表示
    printf("|- [Kind:%d] (line:%d) ", node->kind, node->line);

    switch (node->kind) {
        case AST_CLASS_DEF:
            printf("CLASS_DEF\n");
            dump_ast(node->class_def.class, depth + 1);
            if (node->class_def.super_class) dump_ast(node->class_def.super_class, depth + 1);
            dump_ast(node->class_def.members, depth + 1);
            break;

        case AST_SEND:
            printf("SEND\n");
            if (node->send.receiver) dump_ast(node->send.receiver, depth + 1);
            dump_ast(node->send.message, depth + 1);
            if (node->send.args) dump_ast(node->send.args, depth + 1);
            break;

        case AST_IDENTIFIER:
            printf("ID: %s\n", node->identifier.name);
            break;

        case AST_LITERAL:
            printf("LITERAL (Value: 0x%016llx)\n", (unsigned long long)node->literal.value);
            break;

        case AST_STMT:
            printf("STMT\n");
            dump_ast(node->stmt.left, depth + 1);
            dump_ast(node->stmt.right, depth + 1);
            break;

        case AST_BLOCK:
            printf("BLOCK\n");
            if (node->block.args) dump_ast(node->block.args, depth + 1);
            if (node->block.body) dump_ast(node->block.body, depth + 1);
            break;
            
        case AST_VAR_DECL:
            printf("VAR_DECL\n");
            dump_ast(node->var_decl.identifier, depth + 1);
            dump_ast(node->var_decl.type, depth + 1);
            break;
            
        case AST_CLASS:
            printf("CLASS_TYPE\n");
            dump_ast(node->class.classname, depth + 1);
            break;

        case AST_FIELD_DECL:
            printf("FIELD_DECL (Static:%s)\n", node->field_decl.is_static ? "T" : "F");
            dump_ast(node->field_decl.identifier, depth + 1);
            break;

        default:
            printf("OTHER_NODE\n");
            break;
    }
}
