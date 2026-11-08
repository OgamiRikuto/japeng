#include "compiler_internal.h"
#include "bcd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>

// 文字列プールの復元
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

// TypeInfo の再帰デシリアライズ
static TypeInfo* read_type_info(FILE* fp, Symbol** pool) {
    uint8_t is_present;
    if (fread(&is_present, sizeof(uint8_t), 1, fp) != 1 || !is_present) {
        return NULL;
    }

    uint32_t name_id;
    uint16_t type_arg_count;
    uint8_t  param_count, min_param_count;

    fread(&name_id, sizeof(uint32_t), 1, fp);
    fread(&type_arg_count, sizeof(uint16_t), 1, fp);
    fread(&param_count, sizeof(uint8_t), 1, fp);
    fread(&min_param_count, sizeof(uint8_t), 1, fp);

    Symbol* type_name = pool[name_id];
    TypeInfo* t = new_type_info(type_name, (int)type_arg_count);
    t->param_count = (int)param_count;
    t->min_param_count = (int)min_param_count;

    // デフォルト引数値の復元（簡易スキップまたは復元）
    for (int i = 0; i < t->param_count; i++) {
        uint8_t tag;
        fread(&tag, sizeof(uint8_t), 1, fp);
        if (tag == BCD_VAL_INT) { int32_t val; fread(&val, sizeof(int32_t), 1, fp); t->default_values[i] = make_int(val); }
        else if (tag == BCD_VAL_BOOL) { uint8_t val; fread(&val, sizeof(uint8_t), 1, fp); t->default_values[i] = make_bool(val != 0); }
        else if (tag == BCD_VAL_FLOAT) { double val; fread(&val, sizeof(double), 1, fp); t->default_values[i] = make_float(val); }
        else if (tag == BCD_VAL_STRING) { uint32_t sid; fread(&sid, sizeof(uint32_t), 1, fp); t->default_values[i] = make_obj((Obj*)pool[sid]); }
        else { t->default_values[i] = make_nil(); }
    }

    for (int i = 0; i < t->type_arg_count; i++) {
        t->type_args[i] = read_type_info(fp, pool);
    }
    return t;
}

// .bcd ファイルから型メタデータだけを読み込んで ClassDef を構築
ClassDef* load_class_type_from_bcd(Compiler* c, const char* bcd_path) 
{
    FILE* fp = fopen(bcd_path, "rb");
    if (!fp) return NULL;

    BcdHeader header;
    if (fread(&header, sizeof(BcdHeader), 1, fp) != 1) { fclose(fp); return NULL; }
    if (memcmp(header.magic, BCD_MAGIC, 4) != 0 || header.type_meta_offset == 0) {
        fclose(fp);
        return NULL;
    }

    // 文字列プールを復元
    uint32_t pool_count = 0;
    Symbol** pool = load_string_pool(fp, &pool_count);

    // ★ 型メタデータセクションへジャンプ！
    fseek(fp, header.type_meta_offset, SEEK_SET);

    uint8_t is_iface = 0;
    fread(&is_iface, sizeof(uint8_t), 1, fp);

    // ① クラス型
    TypeInfo* class_type = read_type_info(fp, pool);
    Symbol* class_name = class_type ? class_type->name : NULL;
    if (!class_name) { free(pool); fclose(fp); return NULL; }

    // ② 親クラス型
    TypeInfo* super_type = read_type_info(fp, pool);
    ClassDef* superclass = super_type ? resolve_class(c, super_type->name) : NULL;

    ClassDef* klass = new_class(class_name, class_type, superclass, super_type, 0);
    klass->is_interface = (is_iface != 0);

    // 委譲型リストの完全復元
    uint16_t del_count = 0;
    fread(&del_count, sizeof(uint16_t), 1, fp);
    for (int i = 0; i < del_count; i++) {
        TypeInfo* dt = read_type_info(fp, pool);
        (void)dt; // 必要に応じて klass->delegate_types にセット
    }

    // インターフェースの型復元
    uint16_t iface_cnt;
    fread(&iface_cnt, sizeof(uint16_t), 1, fp);
    klass->interface_count = iface_cnt;
    if (iface_cnt > 0) {
        klass->interfaces = (ClassDef**)malloc(sizeof(ClassDef*) * iface_cnt);
        klass->interface_types = (TypeInfo**)malloc(sizeof(TypeInfo*) * iface_cnt);
        for (int i = 0; i < iface_cnt; i++) {
            klass->interface_types[i] = read_type_info(fp, pool);
            // インターフェースの ClassDef 実体は名前から引いて接続
            klass->interfaces[i] = klass->interface_types[i] 
                ? resolve_class(c, klass->interface_types[i]->name) 
                : NULL;
        }
    }

    // フィールド型リストの完全復元
    uint16_t f_count = 0;
    fread(&f_count, sizeof(uint16_t), 1, fp);
    for (int i = 0; i < f_count; i++) {
        uint32_t fn_id = 0;
        uint8_t flags = 0;
        fread(&fn_id, sizeof(uint32_t), 1, fp);
        fread(&flags, sizeof(uint8_t), 1, fp);
        TypeInfo* ft = read_type_info(fp, pool);

        Symbol* fname = pool[fn_id];
        bool is_static = (flags & 0x01) != 0;
        FieldInfo fi = new_field_info(fname, ft, NULL, is_static);
        add_field(klass, fi, make_nil());
    }

    // vtable スロット順にメソッドシグネチャを復元 (スロット番号ズレゼロ)
    uint16_t vt_count = 0;
    fread(&vt_count, sizeof(uint16_t), 1, fp);
    for (int i = 0; i < vt_count; i++) {
        uint32_t mid;
        fread(&mid, sizeof(uint32_t), 1, fp);
        TypeInfo* fn_type = read_type_info(fp, pool);

        Symbol* mname = pool[mid];
        
        if (fn_type != NULL) {
            add_method_type(klass, mname, fn_type);
        }
        // スロット番号を書き込まれた通りの順序で確定確保
        add_or_override_vtable_slot(klass, mname, make_nil());
    }

    uint16_t sf_count = 0;
    if (fread(&sf_count, sizeof(uint16_t), 1, fp) == 1 && sf_count > 0) {
        for (int i = 0; i < sf_count; i++) {
            uint32_t sfn_id = 0;
            fread(&sfn_id, sizeof(uint32_t), 1, fp);
            TypeInfo* sft = read_type_info(fp, pool);

            Symbol* sfname = pool[sfn_id];
            FieldInfo fi = new_field_info(sfname, sft, NULL, true);
            add_static_field(klass, fi, make_nil());
        }
    }

    uint16_t sm_count = 0;
    if (fread(&sm_count, sizeof(uint16_t), 1, fp) == 1 && sm_count > 0) {
        for (int i = 0; i < sm_count; i++) {
            uint32_t smid = 0;
            fread(&smid, sizeof(uint32_t), 1, fp);
            TypeInfo* sm_type = read_type_info(fp, pool);

            Symbol* smname = pool[smid];
            if (sm_type != NULL) {
                add_method_type(klass, smname, sm_type);
            }
            add_static_method(klass, smname, make_nil());
        }
    }

    klass->state = CLASS_STATE_COMPILED;

    if (c->defined_class == NULL) c->defined_class = new_table(16);
    table_set(c->defined_class, class_name, make_obj((Obj*)klass));

    free(pool);
    fclose(fp);
    return klass;
}

// キャッシュファイルが元ソースに対して有効か検証
bool is_cache_fresh(const char* bcd_path, const char* base_dir) 
{
    struct stat bcd_st;
    if (stat(bcd_path, &bcd_st) != 0) return false; // .bcd が存在しない

    FILE* fp = fopen(bcd_path, "rb");
    if (!fp) return false;

    BcdHeader header;
    if (fread(&header, sizeof(BcdHeader), 1, fp) != 1) {
        fclose(fp);
        return false;
    }
    fclose(fp);

    // ヘッダに記録された親ソースが存在しない場合はキャッシュを採用
    if (header.source_file[0] == '\0') return true;

    char cd_path[1024];
    snprintf(cd_path, sizeof(cd_path), "%s/%s", base_dir, header.source_file);

    struct stat cd_st;
    if (stat(cd_path, &cd_st) != 0) {
        // 親ソースが見つからない場合（配布ライブラリ等）は .bcd をそのまま信用
        return true;
    }

    // ★ ソースの最終更新日時（mtime）が .bcd より新しければ腐敗（false）判定！
    if (cd_st.st_mtime > bcd_st.st_mtime) {
        return false; 
    }

    return true; // キャッシュは新鮮
}
