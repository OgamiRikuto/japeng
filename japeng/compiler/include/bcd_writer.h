#ifndef BCD_WRITER_H
#define BCD_WRITER_H

#include "compiler.h"
#include "bcd.h"

// .je_cache ディレクトリが存在することを確認（無ければ作成）
bool ensure_cache_dir(const char* base_dir);

// .bcd ファイルから型メタデータだけを読み込んで ClassDef を構築
ClassDef* load_class_type_from_bcd(Compiler* c, const char* bcd_path);

// キャッシュファイルが元ソースに対して有効か検証
bool is_cache_fresh(const char* bcd_path, const char* base_dir); 

// 1つのクラス定義を <base_dir>/.je_cache/<ClassName>.bcd に書き出す
bool write_class_bcd(const char* base_dir, ClassDef* klass, const char* source_filename);

// トップレベルスクリプトを <base_dir>/.je_cache/<script_name>.bje に書き出す
bool write_script_bje(const char* base_dir, const char* script_name, Chunk* chunk);

#endif
