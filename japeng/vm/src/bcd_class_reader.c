#include "vm.h"
#include "bcd_class_reader.h"
#include "bcd.h"
#include "chunk.h"
#include "bigint.h"
#include "decimal.h"
#include "table.h"
#include "gc_heap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

Value vm_read_value(FILE* fp, Symbol** pool, VM* vm);

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

bool vm_read_chunk_data(FILE* fp, Chunk* chunk, Symbol** pool, VM* vm) {
    uint32_t code_count;
    if (fread(&code_count, sizeof(uint32_t), 1, fp) != 1) return false;

    chunk->count = chunk->capacity = (int)code_count;
    chunk->code = (uint32_t*)malloc(sizeof(uint32_t) * code_count);
    if (code_count > 0 && fread(chunk->code, sizeof(uint32_t), code_count, fp) != code_count) return false;

    uint32_t const_count;
    if (fread(&const_count, sizeof(uint32_t), 1, fp) != 1) return false;

    chunk->constant_count = chunk->constant_capacity = (int)const_count;
    chunk->constants = (Value*)malloc(sizeof(Value) * const_count);
    for (uint32_t i = 0; i < const_count; i++) {
        chunk->constants[i] = vm_read_value(fp, pool, vm);
    }
    return true;
}

static ObjFunction* read_function(FILE* fp, Symbol** pool, VM* vm) {
    uint8_t arity;
    uint16_t up_count;
    fread(&arity, sizeof(uint8_t), 1, fp);
    fread(&up_count, sizeof(uint16_t), 1, fp);

    Chunk* chunk = (Chunk*)malloc(sizeof(Chunk));
    init_chunk(chunk);

    ObjFunction* fn = new_function(chunk, arity);
    fn->upvalue_count = (int)up_count;
    if (up_count > 0) {
        fn->upvalues = (UpvalueInfo*)malloc(sizeof(UpvalueInfo) * up_count);
        for (int i = 0; i < up_count; i++) {
            fread(&fn->upvalues[i].index, sizeof(uint8_t), 1, fp);
            uint8_t is_loc;
            fread(&is_loc, sizeof(uint8_t), 1, fp);
            fn->upvalues[i].is_local = (is_loc != 0);
        }
    } else {
        fn->upvalues = NULL;
    }

    vm_read_chunk_data(fp, chunk, pool, vm);
    return fn;
}

Value vm_read_value(FILE* fp, Symbol** pool, VM* vm) 
{
    uint8_t tag;
    if (fread(&tag, sizeof(uint8_t), 1, fp) != 1) return make_nil();

    switch (tag) {
        case BCD_VAL_NIL:       return make_nil();
        case BCD_VAL_BOOL:      { uint8_t b; fread(&b, 1, 1, fp); return make_bool(b != 0); }
        case BCD_VAL_INT:       { int32_t i; fread(&i, sizeof(int32_t), 1, fp); return make_int(i); }
        case BCD_VAL_FLOAT:     { double d; fread(&d, sizeof(double), 1, fp); return make_float(d); }
        case BCD_VAL_STRING: {
            uint32_t id;
            fread(&id, sizeof(uint32_t), 1, fp);
            Symbol* s = pool[id];
            // VM 用の GC ヒープ文字列 (32B Slot) を生成
            ObjString* str = gc_new_string(&vm->heap, s->chars, s->length);
            return make_obj((Obj*)str);
        }
        case BCD_VAL_CLASS_REF: {
            uint32_t id;
            fread(&id, sizeof(uint32_t), 1, fp);
            Symbol* cname = pool[id];
            // クラス参照が来たら遅延解決！
            ObjClass* klass = vm_resolve_class(vm, cname);
            return make_obj((Obj*)klass);
        }
        case BCD_VAL_BIGINT: {
            uint32_t id;
            fread(&id, sizeof(uint32_t), 1, fp);
            Symbol* s = pool[id];
            return make_obj((Obj*)new_bi_from_string(s));
        }
        case BCD_VAL_DECIMAL: {
            uint32_t id; int32_t scale;
            fread(&id, sizeof(uint32_t), 1, fp);
            fread(&scale, sizeof(int32_t), 1, fp);
            Symbol* s = pool[id];
            BigInteger* mantissa = new_bi_from_string(s);
            return make_obj((Obj*)gc_new_decimal(&vm->heap, mantissa, scale));
        }
        case BCD_VAL_FUNCTION: {
            ObjFunction* fn = read_function(fp, pool, vm);
            return make_obj((Obj*)fn);
        }
        default: return make_nil();
    }
}

ObjClass* vm_load_class_bcd(VM* vm, const char* bcd_path) 
{
    FILE* fp = fopen(bcd_path, "rb");
    if (!fp) return NULL;

    BcdHeader header;
    if (fread(&header, sizeof(BcdHeader), 1, fp) != 1 ||
        memcmp(header.magic, BCD_MAGIC, 4) != 0 || 
        header.file_type != BCD_TYPE_CLASS) {
        fclose(fp);
        return NULL;
    }

    uint32_t pool_count = 0;
    Symbol** pool = load_string_pool(fp, &pool_count);
    if (!pool) { fclose(fp); return NULL; }

    uint32_t name_id, super_id;
    fread(&name_id, sizeof(uint32_t), 1, fp);
    fread(&super_id, sizeof(uint32_t), 1, fp);

    Symbol* class_name = pool[name_id];
    Symbol* super_name = (super_id != 0xFFFFFFFF) ? pool[super_id] : NULL;

    ObjClass* superclass = NULL;
    if (super_name != NULL && vm != NULL) {
        Value super_val;
        if (table_get(vm->globals, super_name, &super_val)) {
            superclass = (ObjClass*)as_obj(super_val);
        } else {
            // 親クラスも芋づる式に遅延解決！
            superclass = vm_resolve_class(vm, super_name);
        }
    }
    if (superclass == NULL && vm != NULL && class_name != sym_Object) {
        superclass = vm->class_object;
    }

    // 委譲リストの読み飛ばし
    uint16_t del_count;
    fread(&del_count, sizeof(uint16_t), 1, fp);
    for (int i = 0; i < del_count; i++) {
        uint32_t did;
        fread(&did, sizeof(uint32_t), 1, fp);
    }

    // フィールド読み込み
    uint16_t f_count;
    fread(&f_count, sizeof(uint16_t), 1, fp);

    Symbol** temp_field_names = NULL;
    Value*   temp_default_values = NULL;
    if (f_count > 0) {
        temp_field_names    = (Symbol**)malloc(sizeof(Symbol*) * f_count);
        temp_default_values = (Value*)malloc(sizeof(Value) * f_count);
        for (int i = 0; i < f_count; i++) {
            uint32_t fn_id;
            uint8_t flags;
            fread(&fn_id, sizeof(uint32_t), 1, fp);
            fread(&flags, sizeof(uint8_t), 1, fp);
            temp_field_names[i]    = pool[fn_id];
            temp_default_values[i] = vm_read_value(fp, pool, vm);
        }
    }

    ObjClass* klass = vm_new_class(class_name, superclass);

    if (f_count > 0) {
        klass->field_count    = f_count;
        klass->field_names    = (Symbol**)realloc(klass->field_names, sizeof(Symbol*) * f_count);
        klass->default_values = (Value*)realloc(klass->default_values, sizeof(Value) * f_count);
        memcpy(klass->field_names, temp_field_names, sizeof(Symbol*) * f_count);
        memcpy(klass->default_values, temp_default_values, sizeof(Value) * f_count);
        free(temp_field_names);
        free(temp_default_values);
    }

    // vtable 読み込み
    uint16_t vt_count;
    fread(&vt_count, sizeof(uint16_t), 1, fp);

    if (vt_count > klass->vtable_count) {
        klass->vtable_capacity = vt_count;
        klass->vtable       = (Value*)realloc(klass->vtable, sizeof(Value) * vt_count);
        klass->vtable_names = (Symbol**)realloc(klass->vtable_names, sizeof(Symbol*) * vt_count);
        for (uint16_t i = klass->vtable_count; i < vt_count; i++) {
            klass->vtable[i]       = make_nil();
            klass->vtable_names[i] = NULL;
        }
        klass->vtable_count = vt_count;
    }

    for (int i = 0; i < vt_count; i++) {
        uint32_t mname_id;
        uint8_t kind;
        fread(&mname_id, sizeof(uint32_t), 1, fp);
        fread(&kind, sizeof(uint8_t), 1, fp);

        klass->vtable_names[i] = pool[mname_id];
        if (kind == 1) {
            ObjFunction* fn = read_function(fp, pool, vm);
            klass->vtable[i] = make_obj((Obj*)fn);
        }
    }

    free(pool);
    fclose(fp);

    if (vm != NULL) {
        table_set(vm->globals, class_name, make_obj((Obj*)klass));
    }

    return klass;
}

// ★★★ 遅延ロードのコア実装 ★★★
ObjClass* vm_resolve_class(VM* vm, Symbol* class_name) 
{
    if (class_name == NULL) return NULL;

    // 1. すでにロード済みか確認
    Value val;
    if (table_get(vm->globals, class_name, &val)) {
        return (ObjClass*)as_obj(val);
    }

    struct stat st;
    char path[1024];

    // 2. プロジェクトのキャッシュ (.je_cache/<Name>.bcd) を探す
    const char* base_dir = vm->project_dir ? vm->project_dir : ".";
    get_cache_filepath(base_dir, class_name->chars, "bcd", path, sizeof(path));
    if (stat(path, &st) == 0) {
        return vm_load_class_bcd(vm, path);
    }

    // 3. 標準ライブラリディレクトリ (/usr/local/lib/japeng または ./lib) を探す
    const char* lib_dir = get_japeng_lib_dir();
    if (strstr(lib_dir, "usr") != NULL) {
        // インストール先: /usr/local/lib/japeng/<Name>.bcd
        char enc_name[256];
        encode_cache_name(class_name->chars, enc_name, sizeof(enc_name));
        snprintf(path, sizeof(path), "%s/%s.bcd", lib_dir, enc_name);
    } else {
        // 開発環境: ./lib/.je_cache/<Name>.bcd
        get_cache_filepath(lib_dir, class_name->chars, "bcd", path, sizeof(path));
    }

    if (stat(path, &st) == 0) {
        return vm_load_class_bcd(vm, path);
    }

    return NULL;
}
