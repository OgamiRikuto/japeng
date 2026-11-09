#include "builtins.h"
#include "vm_class.h"
#include "symbol.h"
#include <stdio.h>

// クラス名を見て、ネイティブバインドが必要なら関数を差し込む
void bind_native_class_if_needed(ObjClass* klass) 
{
    if (klass == NULL || klass->name == NULL) return;

    if (klass->name == sym_File) {
        register_file_natives(klass);
        return;
    }
}
