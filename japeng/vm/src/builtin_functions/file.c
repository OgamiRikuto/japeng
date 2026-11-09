#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "builtins.h"
#include "vm.h"
#include "common.h"
#include "gc_heap.h"
#include "gc_layout.h"

// --- ポインタ変換ヘルパー ---
static inline void set_file_ptr(ObjInstance* inst, FILE* fp) {
    uintptr_t ptr = (uintptr_t)fp;
    inst->fields[0] = make_int((int32_t)((ptr >> 32) & 0xFFFFFFFF));
    inst->fields[1] = make_int((int32_t)(ptr & 0xFFFFFFFF));
}

static inline FILE* get_file_ptr(ObjInstance* inst) {
    uint32_t hi = (uint32_t)as_int(inst->fields[0]);
    uint32_t lo = (uint32_t)as_int(inst->fields[1]);
    return (FILE*)(((uintptr_t)hi << 32) | (uintptr_t)lo);
}

// -------------------------------------------------------------
// 1. init(path: String, mode: String)
// -------------------------------------------------------------
static bool native_file_init(VM* vm, uint8_t argc) 
{
    Value mode_val = pop(vm);
    Value path_val = pop(vm);
    Value self_val = peek(vm, 0); // レシーバ自身 (File インスタンス)

    ObjString* path_str = (ObjString*)as_obj(path_val);
    ObjString* mode_str = (ObjString*)as_obj(mode_val);
    ObjInstance* self   = (ObjInstance*)as_obj(self_val);

    const char* path = string_chars(path_str);
    const char* mode = string_chars(mode_str);

    FILE* fp = fopen(path, mode);
    if (!fp) {
        set_file_ptr(self, NULL);
        self->fields[2] = make_bool(true); // 失敗時は最初からクローズ状態
        // 必要に応じて例外やエラー通知
    } else {
        set_file_ptr(self, fp);
        self->fields[2] = make_bool(false);
    }

    push(vm, make_nil());
    return true;
}

// -------------------------------------------------------------
// 2. read_line() -> String
// -------------------------------------------------------------
static bool native_file_read_line(VM* vm, uint8_t argc) 
{
    Value self_val = pop(vm);
    ObjInstance* self = (ObjInstance*)as_obj(self_val);

    bool is_closed = as_bool(self->fields[2]);
    FILE* fp = get_file_ptr(self);

    if (is_closed || fp == NULL || feof(fp)) {
        ObjString* empty = gc_new_string(&vm->heap, "", 0);
        push(vm, make_obj((Obj*)empty));
        return true;
    }

    char* line = NULL;
    size_t linecap = 0;
    ssize_t linelen = getline(&line, &linecap, fp);

    if (linelen < 0) {
        if (line) free(line);
        ObjString* empty = gc_new_string(&vm->heap, "", 0);
        push(vm, make_obj((Obj*)empty));
        return true;
    }

    // 末尾の改行 (\r, \n) をトリムしたい場合はここで調整
    if (linelen > 0 && line[linelen - 1] == '\n') {
        linelen--;
        if (linelen > 0 && line[linelen - 1] == '\r') {
            linelen--;
        }
    }

    ObjString* result = gc_new_string(&vm->heap, line, (size_t)linelen);
    free(line);

    push(vm, make_obj((Obj*)result));
    return true;
}

// -------------------------------------------------------------
// 3. read_all() -> String
// -------------------------------------------------------------
static bool native_file_read_all(VM* vm, uint8_t argc) 
{
    Value self_val = pop(vm);
    ObjInstance* self = (ObjInstance*)as_obj(self_val);

    bool is_closed = as_bool(self->fields[2]);
    FILE* fp = get_file_ptr(self);

    if (is_closed || fp == NULL) {
        ObjString* empty = gc_new_string(&vm->heap, "", 0);
        push(vm, make_obj((Obj*)empty));
        return true;
    }

    // 現在位置から末尾までの長さを取得
    long current_pos = ftell(fp);
    fseek(fp, 0, SEEK_END);
    long end_pos = ftell(fp);
    fseek(fp, current_pos, SEEK_SET);

    size_t size = (size_t)(end_pos - current_pos);
    char* buf = (char*)malloc(size + 1);
    size_t read_bytes = fread(buf, 1, size, fp);
    buf[read_bytes] = '\0';

    ObjString* result = gc_new_string(&vm->heap, buf, read_bytes);
    free(buf);

    push(vm, make_obj((Obj*)result));
    return true;
}

// -------------------------------------------------------------
// 4. write(text: String) -> Boolean
// -------------------------------------------------------------
static bool native_file_write(VM* vm, uint8_t argc) 
{
    Value text_val = pop(vm);
    Value self_val = pop(vm);

    ObjString* text   = (ObjString*)as_obj(text_val);
    ObjInstance* self = (ObjInstance*)as_obj(self_val);

    bool is_closed = as_bool(self->fields[2]);
    FILE* fp = get_file_ptr(self);

    if (is_closed || fp == NULL) {
        push(vm, make_bool(false));
        return true;
    }

    const char* str = string_chars(text);
    size_t written = fwrite(str, 1, text->length, fp);

    push(vm, make_bool(written == text->length));
    return true;
}

// -------------------------------------------------------------
// 5. write_line(text: String) -> Boolean
// -------------------------------------------------------------
static bool native_file_write_line(VM* vm, uint8_t argc) 
{
    Value text_val = pop(vm);
    Value self_val = pop(vm);

    ObjString* text   = (ObjString*)as_obj(text_val);
    ObjInstance* self = (ObjInstance*)as_obj(self_val);

    bool is_closed = as_bool(self->fields[2]);
    FILE* fp = get_file_ptr(self);

    if (is_closed || fp == NULL) {
        push(vm, make_bool(false));
        return true;
    }

    const char* str = string_chars(text);
    fwrite(str, 1, text->length, fp);
    fputc('\n', fp);

    push(vm, make_bool(true));
    return true;
}

// -------------------------------------------------------------
// 6. is_eof() -> Boolean
// -------------------------------------------------------------
static bool native_file_is_eof(VM* vm, uint8_t argc) 
{
    Value self_val = pop(vm);
    ObjInstance* self = (ObjInstance*)as_obj(self_val);

    bool is_closed = as_bool(self->fields[2]);
    FILE* fp = get_file_ptr(self);

    if (is_closed || fp == NULL) {
        push(vm, make_bool(true));
        return true;
    }

    push(vm, make_bool(feof(fp) != 0));
    return true;
}

// -------------------------------------------------------------
// 7. close() -> Void
// -------------------------------------------------------------
static bool native_file_close(VM* vm, uint8_t argc) 
{
    Value self_val = pop(vm);
    ObjInstance* self = (ObjInstance*)as_obj(self_val);

    bool is_closed = as_bool(self->fields[2]);
    if (!is_closed) {
        FILE* fp = get_file_ptr(self);
        if (fp != NULL) {
            fclose(fp);
        }
        set_file_ptr(self, NULL);
        self->fields[2] = make_bool(true);
    }

    push(vm, make_nil());
    return true;
}

void register_file_natives(ObjClass* klass) 
{
    vm_define_native(klass, "init",       native_file_init);
    vm_define_native(klass, "read_line",  native_file_read_line);
    vm_define_native(klass, "read_all",   native_file_read_all);
    vm_define_native(klass, "write",      native_file_write);
    vm_define_native(klass, "write_line", native_file_write_line);
    vm_define_native(klass, "is_eof",     native_file_is_eof);
    vm_define_native(klass, "close",      native_file_close);
}
