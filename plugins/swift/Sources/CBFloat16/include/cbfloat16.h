#ifndef CBFLOAT16_H
#define CBFLOAT16_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

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
    memcpy(&bits, &v, 4);

    // NaN: quiet the payload
    if ((bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0) {
        return (uint16_t)((bits >> 16) | 0x0040u);
    }

    // Round to nearest even
    uint32_t lsb = (bits >> 16) & 1;
    return (uint16_t)((bits + 0x7FFFu + lsb) >> 16);
#endif
}

CBFLOAT16_AI float cbfloat16_to_float(uint16_t v) {
#if CBFLOAT16_HAS_NATIVE
    cbfloat16_union_ u;
    u.i = v;
    return (float)u.f;
#else
    uint32_t bits = (uint32_t)v << 16;
    float f;
    memcpy(&f, &bits, 4);
    return f;
#endif
}

CBFLOAT16_AI uint16_t cbfloat16_from_double(double v) {
    return cbfloat16_from_float((float)v);
}

CBFLOAT16_AI uint16_t cbfloat16_add(uint16_t a, uint16_t b) {
    return cbfloat16_from_float(cbfloat16_to_float(a) + cbfloat16_to_float(b));
}

CBFLOAT16_AI uint16_t cbfloat16_sub(uint16_t a, uint16_t b) {
    return cbfloat16_from_float(cbfloat16_to_float(a) - cbfloat16_to_float(b));
}

CBFLOAT16_AI uint16_t cbfloat16_mul(uint16_t a, uint16_t b) {
    return cbfloat16_from_float(cbfloat16_to_float(a) * cbfloat16_to_float(b));
}

CBFLOAT16_AI uint16_t cbfloat16_div(uint16_t a, uint16_t b) {
    return cbfloat16_from_float(cbfloat16_to_float(a) / cbfloat16_to_float(b));
}

CBFLOAT16_AI uint16_t cbfloat16_fma(uint16_t a, uint16_t b, uint16_t c) {
    return cbfloat16_from_float(fmaf(cbfloat16_to_float(a), cbfloat16_to_float(b), cbfloat16_to_float(c)));
}

CBFLOAT16_AI uint16_t cbfloat16_sqrt(uint16_t v) {
    return cbfloat16_from_float(sqrtf(cbfloat16_to_float(v)));
}

CBFLOAT16_AI uint16_t cbfloat16_neg(uint16_t v) {
    return v ^ 0x8000u;
}

CBFLOAT16_AI uint16_t cbfloat16_abs(uint16_t v) {
    return v & 0x7FFFu;
}

CBFLOAT16_AI bool cbfloat16_equal(uint16_t a, uint16_t b) {
    return cbfloat16_to_float(a) == cbfloat16_to_float(b);
}

CBFLOAT16_AI bool cbfloat16_lt(uint16_t a, uint16_t b) {
    return cbfloat16_to_float(a) < cbfloat16_to_float(b);
}

CBFLOAT16_AI bool cbfloat16_lte(uint16_t a, uint16_t b) {
    return cbfloat16_to_float(a) <= cbfloat16_to_float(b);
}

#endif // CBFLOAT16_H
