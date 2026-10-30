#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "ast.h"
#include "defs.h"
#include "parse.h"
#include "symbol.h"
#include "object.h"
#include "error.h"
#include "preprocessor.h"

typedef struct yy_buffer_state *YY_BUFFER_STATE;
extern YY_BUFFER_STATE yy_scan_string(const char *str);
extern void yy_delete_buffer(YY_BUFFER_STATE buffer);

extern FILE* yyin;
extern int linecounter;
extern int yylineno;
void yyrestart(FILE* input_file);
extern const char* current_filename;

ASTNode* current_parsed_ast = NULL;

ASTNode* parsed_cd[MAX_FILES];
int      parsed_cd_count = 0;

ASTNode* parsed_je[MAX_FILES];
int parsed_je_count = 0;;

char* format_code(const char* src);

static int is_cd_file(const char* filename) 
{
    const char* dot = strrchr(filename, '.');
    if(dot == NULL) return 0;
    return (strcmp(dot, ".cd") == 0);
}

static int is_je_file(const char* filename)
{
    const char* dot = strrchr(filename, '.');
    if(dot == NULL) return 0;
    return (strcmp(dot, ".je") == 0);
}

static int is_jje_file(const char* filename) {
    const char* dot = strrchr(filename, '.');
    if (dot == NULL) return 0;
    return (strcmp(dot, ".jje") == 0);
}

static int is_jcd_file(const char* filename) {
    const char* dot = strrchr(filename, '.');
    if (dot == NULL) return 0;
    return (strcmp(dot, ".jcd") == 0);
}

static int is_jp_file(const char* filename) {
    return (is_jje_file(filename) || is_jcd_file(filename));
}

static int is_target_file(const char* filename)
{
    return is_cd_file(filename) || is_je_file(filename) ||
           is_jje_file(filename) || is_jcd_file(filename);
}

bool parse_file(const char* full_path) 
{
    if (!is_target_file(full_path)) {
        fprintf(stderr, "Warning: '%s' is not a .cd or .je or .jje or .jcd file. Skipped.\n", full_path);
        return false;
    }

    FILE* file = fopen(full_path, "r");
    if (!file) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", full_path);
        return false;
    }
    bool success = false;
    current_filename = strdup(full_path);
    linecounter = 1;
    yylineno = 1;
    current_parsed_ast = NULL;

    if (is_jp_file(full_path)) {
        compile_lang = LANG_JA;
        // --- 日本語パイプライン ---
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);

        char* raw_code = malloc(size + 1);
        fread(raw_code, 1, size, file);
        raw_code[size] = '\0';
        fclose(file);

        char* formatted_code = format_code(raw_code);

        if (formatted_code && strcmp(raw_code, formatted_code) != 0) {
            FILE* wfp = fopen(full_path, "w");
            if (wfp) {
                fwrite(formatted_code, 1, strlen(formatted_code), wfp);
                fclose(wfp);
                fprintf(stderr, "整形上書き完了: %s\n", full_path);
            }
        }
        free(raw_code);

        char* clean_code = preprocess(formatted_code);
        fprintf(stderr, "=== PREPROCESS OUTPUT ===\n%s\n=========================\n", clean_code);
        YY_BUFFER_STATE bp = yy_scan_string(clean_code);

        if (yyparse() == 0 && current_parsed_ast != NULL && syntax_error_count == 0) {
            success = true;
        }

        yy_delete_buffer(bp);
        free(formatted_code);
        free(clean_code);
    } else {
        // --- 英語パイプライン ---
        compile_lang = LANG_EN;
        yyin = file;
        yyrestart(file);
        if (yyparse() == 0 && current_parsed_ast != NULL && syntax_error_count == 0) {
            success = true;
        }
        fclose(file);
    }

    if (success) {
        current_parsed_ast = fix_precedence(current_parsed_ast);
        current_parsed_ast = fold_constants(current_parsed_ast);
        if (is_cd_file(full_path) || is_jcd_file(full_path)) {
            if (parsed_cd_count < MAX_FILES) {
                parsed_cd[parsed_cd_count++] = current_parsed_ast;
            } else {
                fprintf(stderr, "Error: Too many .cd files.\n");
            }
        } else if (is_je_file(full_path) || is_jje_file(full_path)) {
            if (parsed_je_count < MAX_FILES) {
                parsed_je[parsed_je_count++] = current_parsed_ast;
            } else {
                fprintf(stderr, "Error: Too many .je files.\n");
            }
        }
    }
    
    // fclose(file);
    return success;
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

        if(S_ISDIR(path_stat.st_mode)) {
            parse(full_path);
        } else if(S_ISREG(path_stat.st_mode)) {
            if(is_target_file(entry->d_name)) {
                parse_file(full_path);
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
            if (node->class_def.from_classes) {
                for (int i = 0; i <= depth; i++) printf("  | ");
                printf(" [FROM_CLASSES]\n");
                print_ast(node->class_def.from_classes, depth + 2);
            }
            print_ast(node->class_def.members, depth + 1);
            break;

        case AST_FUNC_TYPE:
            printf("[FUNC_TYPE]\n");
            if (node->func_type.params) {
                for (int i = 0; i <= depth; i++) printf("  | ");
                printf(" [PARAMS]\n");
                print_ast(node->func_type.params, depth + 2);
            }
            if (node->func_type.rets) {
                for (int i = 0; i <= depth; i++) printf("  | ");
                printf(" [RETURNS]\n");
                print_ast(node->func_type.rets, depth + 2);
            }
            break;
            
        case AST_FIELD_DECL:
            printf("[FIELD_DECL] (static: %s)\n", node->field_decl.is_static ? "true" : "false");
            print_ast(node->field_decl.identifier, depth + 1);
            print_ast(node->field_decl.type, depth + 1);
            if (node->field_decl.from_class) {
                for (int i = 0; i <= depth; i++) printf("  | ");
                printf("  └─ [From]:\n");
                print_ast(node->field_decl.from_class, depth + 1);
            }
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
            printf("[ID: %s]\n", node->identifier.name->chars);
            break;
            
        case AST_LITERAL:
            // 今回はシンプルに値のポインタ/生データを16進数で表示 (NaN Boxing対応)
            printf("[LITERAL] (Kind: %d, Raw: %s)\n", 
                    node->literal.kind, 
                    node->literal.raw ? node->literal.raw->chars : "null");
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
            
        case AST_INDEX_ACCESS:
            printf("[INDEX_ACCESS]\n");
            for (int i = 0; i <= depth; i++) printf("  | ");
            printf(" [TARGET]\n");
            print_ast(node->index_access.target, depth + 2);
            for (int i = 0; i <= depth; i++) printf("  | ");
            printf(" [INDEX]\n");
            print_ast(node->index_access.index, depth + 2);
            break;
        case AST_CONSTRUCT:
            printf("[CONSTRUCT: %s]\n", node->construct.class_name ? node->construct.class_name->chars : "anon");
            if (node->construct.args) {
                for (int i = 0; i <= depth; i++) printf("  | ");
                printf(" [ARGS]\n");
                print_ast(node->construct.args, depth + 2);
            }
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
    printf("|- [Kind:%d] (line:%d) ", node->kind, node->loc.line);

    switch (node->kind) {
        case AST_CLASS_DEF:
            printf("CLASS_DEF\n");
            dump_ast(node->class_def.class, depth + 1);
            if (node->class_def.super_class) dump_ast(node->class_def.super_class, depth + 1);
            if (node->class_def.from_classes) dump_ast(node->class_def.from_classes, depth + 1);
            dump_ast(node->class_def.members, depth + 1);
            break;

        case AST_FUNC_TYPE:
            printf("FUNC_TYPE\n");
            if (node->func_type.params) dump_ast(node->func_type.params, depth + 1);
            if (node->func_type.rets) dump_ast(node->func_type.rets, depth + 1);
            break;

        case AST_SEND:
            printf("SEND\n");
            if (node->send.receiver) dump_ast(node->send.receiver, depth + 1);
            dump_ast(node->send.message, depth + 1);
            if (node->send.args) dump_ast(node->send.args, depth + 1);
            break;

        case AST_IDENTIFIER:
            printf("ID: %s\n", node->identifier.name->chars);
            break;

        case AST_LITERAL:
            printf("[LITERAL] (Kind: %d, Raw: %s)\n", 
                    node->literal.kind, 
                    node->literal.raw ? node->literal.raw->chars : "null");
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
