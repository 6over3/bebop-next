#ifndef CBFLOAT16_H
#define CBFLOAT16_H

#include <stdint.h>

#if __has_attribute(__const__)
#define CBFLOAT16_AI static __inline__ __attribute__((__const__, __always_inline__, __nodebug__))
#else
#define CBFLOAT16_AI static __inline__ __attribute__((__always_inline__, __nodebug__))
#endif

// Native __bf16 detection
#if defined(__BFLT16_MAX__)
#define CBFLOAT16_HAS_NATIVE 1
#elif defined(__clang__) \
    && (defined(__aarch64__) || defined(__arm__) || defined(__riscv) || defined(__loongarch__) \
        || ((defined(__x86_64__) || defined(__i386__)) && defined(__SSE2__)))
#define CBFLOAT16_HAS_NATIVE 1
#else
#define CBFLOAT16_HAS_NATIVE 0
#endif

#if CBFLOAT16_HAS_NATIVE
typedef union { __bf16 f; uint16_t i; } cbfloat16_union_;
#endif

CBFLOAT16_AI uint16_t cbfloat16_from_float(float v) {
#if CBFLOAT16_HAS_NATIVE
    cbfloat16_union_ u;
    u.f = (__bf16)v;
    return u.i;
#else
    uint32_t bits;
    __builtin_memcpy_inline(&bits, &v, 4);

    // NaN: quiet the payload
    if ((bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0) {
        return (uint16_t)((bits >> 16) | 0x0040u);
    }

    // Round to nearest even
    uint32_t lsb = (bits >> 16) & 1;
    return (uint16_t)((bits + 0x7FFFu + lsb) >> 16);
#endif
}

CBFLOAT16_AI uint16_t cbfloat16_from_double(double v) {
    return cbfloat16_from_float((float)v);
}

#endif // CBFLOAT16_H
