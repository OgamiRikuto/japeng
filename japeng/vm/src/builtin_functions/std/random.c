#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include "vm.h"
#include "builtins.h"
// #include "bcd_class_reader.h"

static uint64_t rng_state = 0;

static uint64_t next_u64(void) 
{
    if (rng_state == 0) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        rng_state = ((uint64_t)ts.tv_sec << 32) ^ (uint64_t)ts.tv_nsec ^ 0x9E3779B97F4A7C15ULL;
        if (rng_state == 0) rng_state = 88172645463325252ULL;
    }
    uint64_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng_state = x;
    return x;
}

// Random.seed(s: Integer) -> Void
static bool native_random_seed(VM* vm, uint8_t argc) 
{
    (void)argc;
    Value s_val = pop(vm);
    pop(vm);

    int32_t s = as_int(s_val);
    rng_state = (s == 0) ? 88172645463325252ULL : (uint64_t)(uint32_t)s * 2685821657736338717ULL;

    push(vm, make_nil());
    return true;
}

// Random.randint(min: Integer, max: Integer) -> Integer
static bool native_random_randint(VM* vm, uint8_t argc) 
{
    (void)argc;
    Value max_val = pop(vm);
    Value min_val = pop(vm);
    pop(vm);

    int32_t min_i = as_int(min_val);
    int32_t max_i = as_int(max_val);

    if (min_i >= max_i) {
        push(vm, make_int(min_i));
        return true;
    }

    uint64_t span = (uint64_t)((int64_t)max_i - (int64_t)min_i + 1);
    int32_t res = min_i + (int32_t)(next_u64() % span);

    push(vm, make_int(res));
    return true;
}

// Random.randfloat() -> Float  (0.0 <= r < 1.0)
static bool native_random_randfloat(VM* vm, uint8_t argc) 
{
    (void)argc;
    pop(vm);

    // 上位53bitを使って IEEE754 double の [0.0, 1.0) を生成
    double res = (double)(next_u64() >> 11) * (1.0 / 9007199254740992.0);
    push(vm, make_float(res));
    return true;
}

void register_random_natives(ObjClass* klass) 
{
    vm_define_static_native(klass, "seed",      native_random_seed);
    vm_define_static_native(klass, "randint",   native_random_randint);
    vm_define_static_native(klass, "randfloat", native_random_randfloat);
}
