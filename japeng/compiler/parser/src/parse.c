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
#include "compiler-error.h"
#include "preprocessor.h"

#define OUTPUT_PREPROCESS 0

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
int parsed_je_count = 0;

char* format_code(const char* src);

typedef enum {
    FILE_TARGET_INVALID = 0,
    FILE_TARGET_CLASS_DEF, // クラス定義
    FILE_TARGET_SCRIPT     // スクリプト
} SourceFileType;

// 拡張子からファイル種別を判定し、自然言語フロントエンドがあれば out_fe に渡す
static SourceFileType classify_source_file(const char* ext, LanguageFrontend** out_fe) {
    if (!ext || ext[0] == '\0') return FILE_TARGET_INVALID;

    // 1. 標準コア (英語)
    if (strcmp(ext, "cd") == 0) return FILE_TARGET_CLASS_DEF;
    if (strcmp(ext, "je") == 0) return FILE_TARGET_SCRIPT;

    // 2. 言語フロントエンド登録拡張子 (日本語 .jcd/.jje など)
    LanguageFrontend* fe = find_frontend_by_ext(ext);
    if (fe != NULL) {
        if (out_fe) *out_fe = fe;
        if (fe->ext_class && strcmp(ext, fe->ext_class) == 0)   return FILE_TARGET_CLASS_DEF;
        if (fe->ext_script && strcmp(ext, fe->ext_script) == 0) return FILE_TARGET_SCRIPT;
    }

    return FILE_TARGET_INVALID;
}

bool parse_file(const char* full_path) 
{
    const char* dot = strrchr(full_path, '.');
    const char* ext = dot ? dot + 1 : "";

    LanguageFrontend* fe = NULL;
    SourceFileType file_type = classify_source_file(ext, &fe);

    if (file_type == FILE_TARGET_INVALID) {
        report_warning(ERR_SYS_INVALID_SOURCE_FILE, full_path);
        return false;
    }

    FILE* file = fopen(full_path, "r");
    if (!file) {
        report_system_error(ERR_SYS_CANNOT_OPEN_FILE, full_path);
        return false;
    }

    bool success = false;
    current_filename = strdup(full_path);
    linecounter = 1;
    yylineno = 1;
    current_parsed_ast = NULL;

    set_active_frontend(fe);

    if (fe != NULL) {
        compile_lang = fe->lang;
        // 自然言語パイプライン (既存処理)
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
                report_info(MSG_FORMAT_OVERWRITTEN, full_path);
            }
        }
        free(raw_code);
        
        char* clean_code = frontend_preprocess(formatted_code);
        YY_BUFFER_STATE bp = yy_scan_string(clean_code);
#if OUTPUT_PREPROCESS
        fprintf(stderr, "=== PREPROCESS OUTPUT ===\n%s\n=========================\n", clean_code);
#endif
        
        if (yyparse() == 0 && current_parsed_ast != NULL && syntax_error_count == 0) {
            success = true;
        }

        yy_delete_buffer(bp);
        free(formatted_code);
        free(clean_code);
    } else {
        // 英語パイプライン (既存処理)
        compile_lang = LANG_EN;
        yyin = file;
        yyrestart(file);
        if (yyparse() == 0 && current_parsed_ast != NULL && syntax_error_count == 0) {
            success = true;
        }
        fclose(file);
    }

    // パース成功後の格納先振り分け 
    if (success) {
        current_parsed_ast = fix_precedence(current_parsed_ast);
        current_parsed_ast = fold_constants(current_parsed_ast);

        if (file_type == FILE_TARGET_CLASS_DEF) {
            if (parsed_cd_count < MAX_FILES) {
                parsed_cd[parsed_cd_count++] = current_parsed_ast;
            } else {
                report_system_error(ERR_SYS_TOO_MANY_FILES, ".cd");
            }
        } else if (file_type == FILE_TARGET_SCRIPT) {
            if (parsed_je_count < MAX_FILES) {
                parsed_je[parsed_je_count++] = current_parsed_ast;
            } else {
                report_system_error(ERR_SYS_TOO_MANY_FILES, ".je");
            }
        }
    }
    
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
            const char* dot = strrchr(entry->d_name, '.');
            const char* ext = dot ? dot + 1 : "";
            // 対象ファイルのみ parse_file に回す
            if (classify_source_file(ext, NULL) != FILE_TARGET_INVALID) {
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
            if (node->class_def.is_interface) {
                printf("[INTERFACE_DEF]\n");
            } else {
                printf("[CLASS_DEF]\n");
            }
            print_ast(node->class_def.klass, depth + 1);
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
            if (node->class_def.interfaces) {
                for (int i = 0; i <= depth; i++) printf("  | ");
                printf(" [IMPLEMENTS]\n");
                print_ast(node->class_def.interfaces, depth + 2);
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
            print_ast(node->klass.classname, depth + 1);
            if (node->klass.types) print_ast(node->klass.types, depth + 1); // Generics等
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
            printf("[CONSTRUCT]\n");
            print_ast(node->construct.class_node, depth + 1);
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
            dump_ast(node->class_def.klass, depth + 1);
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
            dump_ast(node->klass.classname, depth + 1);
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
