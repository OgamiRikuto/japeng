#include "bcd_writer.h"
#include "compiler_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MAX_POOL_STRINGS 2048

typedef struct {
    Symbol* strings[MAX_POOL_STRINGS];
    int count;
} BcdStringPool;

static void pool_init(BcdStringPool* pool) {
    pool->count = 0;
}

static uint32_t pool_add(BcdStringPool* pool, Symbol* str) {
    if (str == NULL) return 0xFFFFFFFF;
    for (int i = 0; i < pool->count; i++) {
        if (pool->strings[i] == str) return (uint32_t)i;
    }
    if (pool->count < MAX_POOL_STRINGS) {
        pool->strings[pool->count] = str;
        return (uint32_t)(pool->count++);
    }
    return 0xFFFFFFFF;
}

static void pool_collect_type_names(BcdStringPool* pool, TypeInfo* t) {
    if (t == NULL) return;
    pool_add(pool, t->name);
    for (int i = 0; i < t->type_arg_count; i++) {
        pool_collect_type_names(pool, t->type_args[i]);
    }
}

static void write_string_pool(FILE* fp, BcdStringPool* pool) {
    uint32_t count = (uint32_t)pool->count;
    fwrite(&count, sizeof(uint32_t), 1, fp);
    for (int i = 0; i < pool->count; i++) {
        Symbol* s = pool->strings[i];
        uint16_t len = (uint16_t)s->length;
        fwrite(&len, sizeof(uint16_t), 1, fp);
        fwrite(s->chars, 1, len, fp);
    }
}
static void write_value(FILE* fp, Value val, BcdStringPool* pool);
// Chunk (バイトコード配列 + 定数配列) のシリアライズ
static void write_chunk_data(FILE* fp, Chunk* chunk, BcdStringPool* pool) {
    // 1. 命令列
    uint32_t code_count = (uint32_t)chunk->count;
    fwrite(&code_count, sizeof(uint32_t), 1, fp);
    if (code_count > 0) {
        fwrite(chunk->code, sizeof(uint32_t), code_count, fp);
    }

    // 2. 定数プール
    uint32_t const_count = (uint32_t)chunk->constant_count;
    fwrite(&const_count, sizeof(uint32_t), 1, fp);
    for (int i = 0; i < chunk->constant_count; i++) {
        write_value(fp, chunk->constants[i], pool);
    }
}

// 関数 (ObjFunction) のシリアライズ
static void write_function(FILE* fp, ObjFunction* fn, BcdStringPool* pool) {
    fwrite(&fn->arity, sizeof(uint8_t), 1, fp);
    uint16_t up_count = (uint16_t)fn->upvalue_count;
    fwrite(&up_count, sizeof(uint16_t), 1, fp);
    for (int i = 0; i < fn->upvalue_count; i++) {
        fwrite(&fn->upvalues[i].index, sizeof(uint8_t), 1, fp);
        uint8_t is_loc = fn->upvalues[i].is_local ? 1 : 0;
        fwrite(&is_loc, sizeof(uint8_t), 1, fp);
    }
    write_chunk_data(fp, fn->chunk, pool);
}

// Value を BcdValTag + ペイロードに変換して書き込み
static void write_value(FILE* fp, Value val, BcdStringPool* pool) {
    if (is_nil(val)) {
        uint8_t tag = BCD_VAL_NIL;
        fwrite(&tag, sizeof(uint8_t), 1, fp);
    } else if (is_bool(val)) {
        uint8_t tag = BCD_VAL_BOOL;
        uint8_t b = as_bool(val) ? 1 : 0;
        fwrite(&tag, sizeof(uint8_t), 1, fp);
        fwrite(&b, sizeof(uint8_t), 1, fp);
    } else if (is_int(val)) {
        uint8_t tag = BCD_VAL_INT;
        int32_t i = as_int(val);
        fwrite(&tag, sizeof(uint8_t), 1, fp);
        fwrite(&i, sizeof(int32_t), 1, fp);
    } else if (is_float(val)) {
        uint8_t tag = BCD_VAL_FLOAT;
        double d = as_float(val);
        fwrite(&tag, sizeof(uint8_t), 1, fp);
        fwrite(&d, sizeof(double), 1, fp);
    } else if (is_obj(val)) {
        Obj* obj = as_obj(val);
        if (obj == NULL) {
            uint8_t tag = BCD_VAL_NIL;
            fwrite(&tag, sizeof(uint8_t), 1, fp);
            return;
        }
        if (obj->type == OBJ_STRING) {
            uint8_t tag = BCD_VAL_STRING;
            uint32_t id = pool_add(pool, (Symbol*)obj);
            fwrite(&tag, sizeof(uint8_t), 1, fp);
            fwrite(&id, sizeof(uint32_t), 1, fp);
        } else if (obj->type == OBJ_CLASS) {
            uint8_t tag = BCD_VAL_CLASS_REF;
            uint32_t id = pool_add(pool, ((ClassDef*)obj)->name);
            fwrite(&tag, sizeof(uint8_t), 1, fp);
            fwrite(&id, sizeof(uint32_t), 1, fp);
        } else if (obj->type == OBJ_BIGINTEGER) {
            uint8_t tag = BCD_VAL_BIGINT;
            Symbol* s = bi_to_string((BigInteger*)obj);
            uint32_t id = pool_add(pool, s);
            fwrite(&tag, sizeof(uint8_t), 1, fp);
            fwrite(&id, sizeof(uint32_t), 1, fp);
        } else if (obj->type == OBJ_DECIMAL) {
            uint8_t tag = BCD_VAL_DECIMAL;
            ObjDecimal* dec = (ObjDecimal*)obj;
            Symbol* ms = bi_to_string(dec->mantissa);
            uint32_t id = pool_add(pool, ms);
            int32_t scale = dec->scale;
            fwrite(&tag, sizeof(uint8_t), 1, fp);
            fwrite(&id, sizeof(uint32_t), 1, fp);
            fwrite(&scale, sizeof(int32_t), 1, fp);
        } else if (obj->type == OBJ_FUNCTION) {
            uint8_t tag = BCD_VAL_FUNCTION;
            fwrite(&tag, sizeof(uint8_t), 1, fp);
            write_function(fp, (ObjFunction*)obj, pool);
        } else {
            uint8_t tag = BCD_VAL_NIL;
            fwrite(&tag, sizeof(uint8_t), 1, fp);
        }
    }
}

// TypeInfo の再帰シリアライズ
static void write_type_info(FILE* fp, TypeInfo* t, BcdStringPool* pool) {
    if (t == NULL) {
        uint8_t is_null = 0;
        fwrite(&is_null, sizeof(uint8_t), 1, fp);
        return;
    }
    uint8_t is_present = 1;
    fwrite(&is_present, sizeof(uint8_t), 1, fp);

    uint32_t name_id = pool_add(pool, t->name);
    fwrite(&name_id, sizeof(uint32_t), 1, fp);

    uint16_t type_arg_count = (uint16_t)t->type_arg_count;
    uint8_t  param_count     = (uint8_t)t->param_count;
    uint8_t  min_param_count = (uint8_t)t->min_param_count;
    fwrite(&type_arg_count, sizeof(uint16_t), 1, fp);
    fwrite(&param_count, sizeof(uint8_t), 1, fp);
    fwrite(&min_param_count, sizeof(uint8_t), 1, fp);

    // デフォルト引数値の書き出し
    for (int i = 0; i < t->param_count; i++) {
        write_value(fp, t->default_values[i], pool);
    }

    // 型引数 / 引数・戻り値型の再帰書き出し
    for (int i = 0; i < t->type_arg_count; i++) {
        write_type_info(fp, t->type_args[i], pool);
    }
}

bool ensure_cache_dir(const char* base_dir) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/.je_cache", base_dir);
    struct stat st = {0};
    if (stat(path, &st) == -1) {
#if defined(_WIN32)
        return mkdir(path) == 0;
#else
        return mkdir(path, 0755) == 0;
#endif
    }
    return true;
}

bool write_class_bcd(const char* base_dir, ClassDef* klass, const char* source_filename) 
{
    if (!ensure_cache_dir(base_dir)) return false;

    char filename[1024];
    // ★ 修正 1: get_cache_filepath を使用して安全なエンコード名で保存
    get_cache_filepath(base_dir, klass->name->chars, "bcd", filename, sizeof(filename));

    FILE* fp = fopen(filename, "wb");
    if (!fp) return false;

    BcdHeader header = { {'J', 'P', 'E', 'N'}, BCD_VERSION, BCD_TYPE_CLASS, 0, 0, 0, {0} };
    // ★ 修正 2: 親ソースファイル名をヘッダに正しく記録
    if (source_filename != NULL) {
        strncpy(header.source_file, source_filename, BCD_MAX_SOURCE_NAME - 1);
    }
    fwrite(&header, sizeof(BcdHeader), 1, fp);

    BcdStringPool pool;
    pool_init(&pool);

    // 文字列プール先行収集
    pool_add(&pool, klass->name);
    if (klass->superclass) pool_add(&pool, klass->superclass->name);
    for (int i = 0; i < klass->delegate_count; i++) {
        if (klass->delegates[i]) pool_add(&pool, klass->delegates[i]->name);
    }
    for (int i = 0; i < klass->field_count; i++) {
        pool_add(&pool, klass->field_infos[i].name);
    }
    for (int i = 0; i < klass->vtable_count; i++) {
        pool_add(&pool, klass->vtable_names[i]);
    }
    for (int i = 0; i < klass->vtable_count; i++) {
        Value m = klass->vtable[i];
        if (is_obj_type(m, OBJ_FUNCTION)) {
            ObjFunction* fn = (ObjFunction*)as_obj(m);
            for (int c = 0; c < fn->chunk->constant_count; c++) {
                Value cv = fn->chunk->constants[c];
                if (is_obj_type(cv, OBJ_STRING)) pool_add(&pool, (Symbol*)as_obj(cv));
                if (is_obj_type(cv, OBJ_CLASS)) pool_add(&pool, ((ClassDef*)as_obj(cv))->name);
            }
        }
    }
    pool_collect_type_names(&pool, klass->type);
    pool_collect_type_names(&pool, klass->superclass_type);
    for (int i = 0; i < klass->field_count; i++) {
        pool_collect_type_names(&pool, klass->field_infos[i].type);
    }
    for (int i = 0; i < klass->vtable_count; i++) {
        pool_collect_type_names(&pool, find_method_type(klass, klass->vtable_names[i]));
    }
    for (int i = 0; i < klass->delegate_count; i++) {
        if (klass->delegate_types) pool_collect_type_names(&pool, klass->delegate_types[i]);
    }

    // 1. 文字列プール
    write_string_pool(fp, &pool);

    // 2. VM用クラスデータ
    uint32_t name_id = pool_add(&pool, klass->name);
    uint32_t super_id = klass->superclass ? pool_add(&pool, klass->superclass->name) : 0xFFFFFFFF;
    fwrite(&name_id, sizeof(uint32_t), 1, fp);
    fwrite(&super_id, sizeof(uint32_t), 1, fp);

    uint16_t del_count = (uint16_t)klass->delegate_count;
    fwrite(&del_count, sizeof(uint16_t), 1, fp);
    for (int i = 0; i < klass->delegate_count; i++) {
        uint32_t did = pool_add(&pool, klass->delegates[i]->name);
        fwrite(&did, sizeof(uint32_t), 1, fp);
    }

    uint16_t f_count = (uint16_t)klass->field_count;
    fwrite(&f_count, sizeof(uint16_t), 1, fp);
    for (int i = 0; i < klass->field_count; i++) {
        uint32_t fn_id = pool_add(&pool, klass->field_infos[i].name);
        uint8_t flags = 0;
        if (klass->field_infos[i].is_static) flags |= 0x01;
        if (klass->field_infos[i].is_delegate_instance) flags |= 0x02;
        fwrite(&fn_id, sizeof(uint32_t), 1, fp);
        fwrite(&flags, sizeof(uint8_t), 1, fp);
        write_value(fp, klass->default_values[i], &pool);
    }

    uint16_t vt_count = (uint16_t)klass->vtable_count;
    fwrite(&vt_count, sizeof(uint16_t), 1, fp);
    for (int i = 0; i < klass->vtable_count; i++) {
        uint32_t mname_id = pool_add(&pool, klass->vtable_names[i]);
        fwrite(&mname_id, sizeof(uint32_t), 1, fp);

        Value m = klass->vtable[i];
        if (is_obj_type(m, OBJ_FUNCTION)) {
            uint8_t kind = 1;
            fwrite(&kind, sizeof(uint8_t), 1, fp);
            write_function(fp, (ObjFunction*)as_obj(m), &pool);
        } else {
            uint8_t kind = 0;
            fwrite(&kind, sizeof(uint8_t), 1, fp);
        }
    }

    // =========================================================================
    // ★★★ 3. 型メタデータセクション ★★★
    // =========================================================================
    long type_section_start = ftell(fp);

    write_type_info(fp, klass->type, &pool);
    write_type_info(fp, klass->superclass_type, &pool);

    // 委譲型リスト (個数 + 各型)
    fwrite(&del_count, sizeof(uint16_t), 1, fp);
    for (int i = 0; i < klass->delegate_count; i++) {
        write_type_info(fp, klass->delegate_types ? klass->delegate_types[i] : NULL, &pool);
    }

    // フィールド型リスト (個数 + 各型)
    fwrite(&f_count, sizeof(uint16_t), 1, fp);
    for (int i = 0; i < klass->field_count; i++) {
        write_type_info(fp, klass->field_infos[i].type, &pool);
    }

    // メソッドシグネチャ (★ vtable のスロット順に確定保存！)
    fwrite(&vt_count, sizeof(uint16_t), 1, fp);
    for (int i = 0; i < klass->vtable_count; i++) {
        Symbol* mname = klass->vtable_names[i];
        uint32_t mid = pool_add(&pool, mname);
        fwrite(&mid, sizeof(uint32_t), 1, fp);

        TypeInfo* mt = find_method_type(klass, mname);
        write_type_info(fp, mt, &pool);
    }

    long type_section_end = ftell(fp);

    header.type_meta_offset = (uint32_t)type_section_start;
    header.type_meta_size   = (uint32_t)(type_section_end - type_section_start);

    fseek(fp, 0, SEEK_SET);
    fwrite(&header, sizeof(BcdHeader), 1, fp);

    fclose(fp);
    return true;
}

static void collect_function_strings(ObjFunction* fn, BcdStringPool* pool) {
    if (!fn || !fn->chunk) return;
    for (int c = 0; c < fn->chunk->constant_count; c++) {
        Value cv = fn->chunk->constants[c];
        if (is_obj_type(cv, OBJ_STRING)) pool_add(pool, (Symbol*)as_obj(cv));
        if (is_obj_type(cv, OBJ_CLASS)) pool_add(pool, ((ClassDef*)as_obj(cv))->name);
        if (is_obj_type(cv, OBJ_FUNCTION)) collect_function_strings((ObjFunction*)as_obj(cv), pool);
    }
}

bool write_script_bje(const char* base_dir, const char* script_name, Chunk* chunk) {
    if (!ensure_cache_dir(base_dir)) return false;

    char filename[1024];
    get_cache_filepath(base_dir, script_name, "bje", filename, sizeof(filename));

    FILE* fp = fopen(filename, "wb");
    if (!fp) return false;

    BcdHeader header = { {'J', 'P', 'E', 'N'}, BCD_VERSION, BCD_TYPE_SCRIPT, 0 };
    fwrite(&header, sizeof(BcdHeader), 1, fp);

    BcdStringPool pool;
    pool_init(&pool);

    // 定数内の文字列・クラス参照をプールへ
    for (int i = 0; i < chunk->constant_count; i++) {
        Value cv = chunk->constants[i];
        if (is_obj_type(cv, OBJ_STRING)) pool_add(&pool, (Symbol*)as_obj(cv));
        if (is_obj_type(cv, OBJ_CLASS)) pool_add(&pool, ((ClassDef*)as_obj(cv))->name);
        if (is_obj_type(cv, OBJ_FUNCTION)) collect_function_strings((ObjFunction*)as_obj(cv), &pool);
    }

    write_string_pool(fp, &pool);
    write_chunk_data(fp, chunk, &pool);

    fclose(fp);
    return true;
}
