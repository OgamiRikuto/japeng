#ifndef MODULE_LOADER_H
#define MODULE_LOADER_H

#include "compiler_internal.h"
#include <stdbool.h>

// モジュール探索（カレント -> ~/.japeng/packages/ -> <bin>/../lib/std/）
const char* resolve_module_file_path(const char* mod_name, const char* cur_filename);

// 辞書ファイルのロード（<module_path>.dict.<lang> を読み込み c->dict_aliases に登録）
void load_module_dict(Compiler* c, const char* base_cd_path, const char* cur_filename);

// import ASTノードの処理本体
void compile_import(Compiler* c, ASTNode* node);

#endif // MODULE_LOADER_H_
