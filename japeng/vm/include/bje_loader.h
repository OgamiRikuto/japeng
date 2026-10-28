#ifndef BJE_LOADER_H
#define BJE_LOADER_H

#include "vm.h"
#include "chunk.h"
#include <stdbool.h>

// .je_cache/<script_name>.bje を読み込んで Chunk に展開する
bool load_script_bje(VM* vm, const char* base_dir, const char* script_name, Chunk* out_chunk);

#endif
