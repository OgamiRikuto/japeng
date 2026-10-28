#include "bje_loader.h"
#include "bcd.h"
#include "bcd_class_reader.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// bcd_class_reader.c の関数を活用
extern Value vm_read_value(FILE* fp, Symbol** pool, VM* vm);
extern bool  vm_read_chunk_data(FILE* fp, Chunk* chunk, Symbol** pool, VM* vm);

static Symbol** load_string_pool(FILE* fp, uint32_t* out_count) {
    uint32_t count;
    if (fread(&count, sizeof(uint32_t), 1, fp) != 1) return NULL;

    Symbol** pool = (Symbol**)malloc(sizeof(Symbol*) * count);
    for (uint32_t i = 0; i < count; i++) {
        uint16_t len;
        fread(&len, sizeof(uint16_t), 1, fp);
        char* buf = (char*)malloc(len + 1);
        fread(buf, 1, len, fp);
        buf[len] = '\0';
        pool[i] = intern_string(buf, len);
        free(buf);
    }
    *out_count = count;
    return pool;
}

bool load_script_bje(VM* vm, const char* base_dir, const char* script_name, Chunk* out_chunk) {
    char filename[1024];
    get_cache_filepath(base_dir, script_name, "bje", filename, sizeof(filename));

    FILE* fp = fopen(filename, "rb");
    if (!fp) return false;

    BcdHeader header;
    if (fread(&header, sizeof(BcdHeader), 1, fp) != 1) {
        fclose(fp);
        return false;
    }

    if (memcmp(header.magic, BCD_MAGIC, 4) != 0 || header.file_type != BCD_TYPE_SCRIPT) {
        fclose(fp);
        return false;
    }

    uint32_t pool_count = 0;
    Symbol** pool = load_string_pool(fp, &pool_count);
    if (!pool) {
        fclose(fp);
        return false;
    }

    init_chunk(out_chunk);
    bool ok = vm_read_chunk_data(fp, out_chunk, pool, vm);

    free(pool);
    fclose(fp);
    return ok;
}
