#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include "vm.h"
#include "builtins.h"
// #include "bcd_class_reader.h"

// Clock.now() -> Float
static bool native_clock_now(VM* vm, uint8_t argc) {
    (void)argc;
    pop(vm); // レシーバ (Clock クラス) をポップ

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double sec = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;

    push(vm, make_float(sec));
    return true;
}

// Clock.sleep(seconds: Float) -> Void
static bool native_clock_sleep(VM* vm, uint8_t argc) {
    (void)argc;
    Value sec_val = pop(vm);
    pop(vm); // レシーバ (Clock クラス) をポップ

    double sec = as_float(sec_val);
    if (sec > 0.0) {
        struct timespec req;
        req.tv_sec  = (time_t)sec;
        req.tv_nsec = (long)((sec - (double)req.tv_sec) * 1e9);
        nanosleep(&req, NULL);
    }

    push(vm, make_nil());
    return true;
}

void register_clock_natives(ObjClass* klass) 
{
    vm_define_static_native(klass, "now",   native_clock_now);
    vm_define_static_native(klass, "sleep", native_clock_sleep);
}
