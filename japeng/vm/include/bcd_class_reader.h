#ifndef BCD_CLASS_READER_H
#define BCD_CLASS_READER_H

#include "vm.h"
#include "vm_class.h"

// .bcd ファイルから VM用 ObjClass を復元する (型メタデータは読み飛ばす)
ObjClass* vm_load_class_bcd(VM* vm, const char* bcd_path);

ObjClass* vm_resolve_class(VM* vm, Symbol* class_name);
#endif
