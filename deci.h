/*
 * Copyright (C) 2020  libdeci developers
 *
 * This file is part of libdeci.
 *
 * libdeci is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libdeci is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libdeci.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// deci_* functions work on 'deci_UWORD *' spans representing unsigned little-endian (see below for
// what it means exactly) big integers in base of 'DECI_BASE'.
//
// A (deci_UWORD*) span is a pair of pointers,
//     deci_UWORD *wa
// and
//     deci_UWORD *wa_end,
// also written as (wa ... wa_end), where wa[0], wa[1], ..., wa_end[-1] are defined and are less
// than 'DECI_BASE'.
//
// It is perfectly fine for a 'deci_UWORD *' span to be empty, that is, be represented by
// (ptr ... ptr), for some pointer 'ptr'; such a span represents the value of zero.
//
// In general, a 'deci_UWORD *' span of (wa ... wa_end) represents the value of
//
//     sum for each integer i in [0; wa_end - wa):
//         wa[i] * DECI_BASE ^ i,
//
// where '^' means exponentiation, not xor. That's what we mean by "little-endian" -- that 'wa[0]'
// is the *least* significant digit in base 'DECI_BASE', and 'wa_end[-1]' is the *most* significant.
// This has nothing to do with the actual endianness of the hardware, which we are independent of.
//
// Now, for normalization. To normalize a 'deci_UWORD *' span means simply to remove the leading
// zeroes, thus possibly decrementing its "right bound" by some amount.
//
// Normalization is not *required* (except for 'deci_divmod_unsafe()'), but obviously everything
// will work faster if the numbers are normalized -- you just reduce the size of the spans that
// these functions work on.
//
// If you think about it, whenever you have some kind of a dynamic array, you usually can pop from
// the back, but not from the front of it. For example, C's 'realloc()' can truncate the buffer from
// the right, but not from the left. So, we would like the leading zeros to be at the back of a
// span, not in the beginning -- that's why little-endian is chosen.
//
// Before using, please read the descriptions of the functions carefully -- many of them assume some
// non-obvious invariants.

#if ! defined(DECI_WE_ARE_64_BIT)
#   if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFul
#       define DECI_WE_ARE_64_BIT 1
#   else
#       define DECI_WE_ARE_64_BIT 0
#   endif
#endif

#if ! defined(DECI_UNUSED)
#   if __GNUC__ >= 2
#       define DECI_UNUSED __attribute__((unused))
#   else
#       define DECI_UNUSED /*nothing*/
#   endif
#endif

#if ! defined(DECI_FORCE_INLINE)
#   if __GNUC__ >= 2
#       define DECI_FORCE_INLINE __attribute__((always_inline))
#   elif defined(_MSC_VER)
#       define DECI_FORCE_INLINE __forceinline
#   else
#       define DECI_FORCE_INLINE /*nothing*/
#   endif
#endif

// We *really* want to be able to natively divide 'deci_DOUBLE_UWORD' values, so it has to be
// 64-bit on 64-bit systems, and 32-bit on 32-bit systems.

#if DECI_WE_ARE_64_BIT

typedef uint32_t            deci_UWORD;
typedef int32_t             deci_SWORD;
typedef uint64_t            deci_DOUBLE_UWORD;
typedef int64_t             deci_DOUBLE_SWORD;
#define DECI_BASE_LOG 9
#define DECI_WORD_BITS 32
#define DECI_DOUBLE_WORD_BITS 64
DECI_UNUSED static const deci_UWORD DECI_BASE = 1000000000;
#define DECI_FOR_EACH_TENPOW(X) \
    X(0, 1) \
    X(1, 10) \
    X(2, 100) \
    X(3, 1000) \
    X(4, 10000) \
    X(5, 100000) \
    X(6, 1000000) \
    X(7, 10000000) \
    X(8, 100000000)

#else

typedef uint16_t deci_UWORD;
typedef int16_t  deci_SWORD;
typedef uint32_t deci_DOUBLE_UWORD;
typedef int32_t  deci_DOUBLE_SWORD;
#define DECI_BASE_LOG 4
#define DECI_WORD_BITS 16
#define DECI_DOUBLE_WORD_BITS 32
DECI_UNUSED static const deci_UWORD DECI_BASE = 10000;
#define DECI_FOR_EACH_TENPOW(X) \
    X(0, 1) \
    X(1, 10) \
    X(2, 100) \
    X(3, 1000)

#endif

// Adds (wa ... wa_end) to (wb ... wb_end), writing the result into (wa ... wa_end).
//
// Assumes (wa_end - wa) >= (wb_end - wb); otherwise, the behavior is undefined.
//
// Returns the value of the carry flag after the addition; in other words,
//
//    * if return value is false, the value stored in (wa ... wa_end) is the correct result;
//
//    * if return value is true, the addition overflowed (wa .. wa_end); the most significant word
//      of the result is ((deci_UWORD) 1), and the rest of the words were written into
//      (wa ... wa_end).
bool deci_add(
        deci_UWORD *wa, deci_UWORD *wa_end,
        deci_UWORD *wb, deci_UWORD *wb_end);

// Subtracts (wb ... wb_end) from (wa ... wa_end), writing the result into (wa ... wa_end).
//
// Assumes (wa_end - wa) >= (wb_end - wb); otherwise, the behavior is undefined.
//
// Returns the value of the borrow flag after the subtraction; in other words,
//
//    * if return value is false, the value stored in (wa ... wa_end) is the correct result;
//
//    * if return value is true, the subtraction underflowed, and what was stored in (wa ... wa_end)
//      is the ten's complement value of the result, that is, the value of
//          {1 000 ... 000} + result,
//      where:
//
//          * the {1 000 ... 000} value is in big-decimal notation, that is, the word with value of
//            1 is the *most* significant one, not the *least* significant;
//
//          * the number of '000' words in the minuend is equal to (wa_end - wa);
//
//          * 'result' is negative (since the subtraction underflowed).
//
//      Use 'deci_uncomplement()' to convert the ten's complement value of the negative result to
//      its absolute value.
bool deci_sub_raw(
        deci_UWORD *wa, deci_UWORD *wa_end,
        deci_UWORD *wb, deci_UWORD *wb_end);

// Performs the following subtraction, writing the result into (wa ... wa_end):
//     {1 000 ... 000} - (wa ... wa_end),
// where:
//     * the {1 000 ... 000} value is in big-decimal notation, that is, the word with value of
//       1 is the *most* significant one, not the *least* significant;
//
//     * the number of '000' words in the minuend is equal to (wa_end - wa).
//
// Assumes that (wa ... wa_end) does not represent the value of zero, i.e., that it contains at
// least one non-zero word; otherwise, the behavior is undefined.
void deci_uncomplement(deci_UWORD *wa, deci_UWORD *wa_end);

// Subtracts two (deci_UWORD*) spans, writing the result into (wa ... wa_end).
//
// Assumes (wa_end - wa) >= (wb_end - wb); otherwise, the behavior is undefined.
//
// If the borrow flag after the subtraction is false, this function returns false; otherwise, it
// performs the "uncomplement" operation to recover the negated result of the subtraction, and
// returns true.
//
// In order words,
//
//    * if the return value is false, the value stored in (wa ... wa_end) is the correct result;
//
//    * if the return value is true, the result is actually negative, and what was stored in
//      (wa ... wa_end) is its absolute value.
static inline DECI_UNUSED
bool deci_sub(
        deci_UWORD *wa, deci_UWORD *wa_end,
        deci_UWORD *wb, deci_UWORD *wb_end)
{
    const bool underflow = deci_sub_raw(wa, wa_end, wb, wb_end);
    if (underflow)
        deci_uncomplement(wa, wa_end);
    return underflow;
}

// Multiplies (wa ... wa_end) by 'b'.
//
// Assumes (b < DECI_BASE); otherwise, the behavior is undefined.
//
// Returns the most significant word of the result, writing the rest of the words into
// (wa ... wa_end).
static inline DECI_UNUSED
deci_UWORD deci_mul_uword(
        deci_UWORD *wa, deci_UWORD *wa_end,
        deci_UWORD b)
{
    deci_UWORD carry = 0;
    for (; wa != wa_end; ++wa) {
        const deci_DOUBLE_UWORD x = *wa * ((deci_DOUBLE_UWORD) b) + carry;
        *wa = x % DECI_BASE;
        carry = x / DECI_BASE;
    }
    return carry;
}

// Multiplies (wa ... wa_end) by (wb ... wb_end), writing the result into
//     (out ... out + N),
// where N = (wa_end - wa) + (wb_end - wb).
//
// Assumes 'out' is a pointer to N words, ALL INITIALLY ZEROED OUT; otherwise, the behavior is
// undefined.
void deci_mul(
        deci_UWORD *wa, deci_UWORD *wa_end,
        deci_UWORD *wb, deci_UWORD *wb_end,
        deci_UWORD *out);

// Divides (wa ... wa_end) by 'b', writing the quotient into (wa ... wa_end), and returning the
// remainder.
//
// Assumes (0 < b < DECI_BASE); otherwise, the behavior is undefined.
static inline DECI_UNUSED
deci_UWORD deci_divmod_uword(
        deci_UWORD *wa, deci_UWORD *wa_end,
        deci_UWORD b)
{
    deci_UWORD carry = 0;
    while (wa_end != wa) {
        --wa_end;

        const deci_DOUBLE_UWORD x = *wa_end + DECI_BASE * (deci_DOUBLE_UWORD) carry;
        *wa_end = x / b;
        carry = x % b;
    }
    return carry;
}

// Returns the remainder of division of (wa ... wa_end) by 'b'.
//
// Assumes (0 < b < DECI_BASE); otherwise, the behavior is undefined.
static inline DECI_UNUSED
deci_UWORD deci_mod_uword(
        deci_UWORD *wa, deci_UWORD *wa_end,
        deci_UWORD b)
{
    deci_UWORD carry = 0;
    while (wa_end != wa) {
        --wa_end;

        const deci_DOUBLE_UWORD x = *wa_end + DECI_BASE * (deci_DOUBLE_UWORD) carry;
        carry = x % b;
    }
    return carry;
}

// Divides (wa ... wa_end) by (wb ... wb_end).
//
// Writes the remainder into (wa ... wa + N), where N = (wb_end - wb).
//
// Returns the most significant word of the quotient, writing the rest of the words into
// (wa + N ... wa_end).
//
// Assumes that:
//
//   * (wb ... wb_end) is normalized;
//
//   * (wa_end - wa) >= N >= 2. Note that if N == 1, you should use either 'deci_divmod_uword' or
//       'deci_mod_uword'; and if N == 0, you are dividing by zero -- oops.
deci_UWORD deci_divmod_unsafe(
        deci_UWORD *wa, deci_UWORD *wa_end,
        deci_UWORD *wb, deci_UWORD *wb_end);

// Divides (wa ... wa_end) by (wb ... wb_end).
//
// The quotient is written into (wa ... wa + N), where N is the return value, N <= (wa_end - wa).
// The value of (wa + N ... wa_end) after this function returns is undefined.
//
// Assumes that (wb ... wb_end) does not represent the value of zero.
size_t deci_div(
        deci_UWORD *wa, deci_UWORD *wa_end,
        deci_UWORD *wb, deci_UWORD *wb_end);

// Divides (wa ... wa_end) by (wb ... wb_end).
//
// The remainder is written into (wa ... wa + N), where N is the return value, N <= (wa_end - wa).
// The value of (wa + N ... wa_end) after this function returns is undefined.
//
// Assumes that (wb ... wb_end) does not represent the value of zero.
size_t deci_mod(
        deci_UWORD *wa, deci_UWORD *wa_end,
        deci_UWORD *wb, deci_UWORD *wb_end);

// Checks if (wa ... wa_end) represents the value of zero, i.e., that all its words are zero.
static inline DECI_UNUSED DECI_FORCE_INLINE
bool deci_is_zero(deci_UWORD *wa, deci_UWORD *wa_end)
{
    for (; wa != wa_end; ++wa)
        if (*wa)
            return false;
    return true;
}

// Checks if (wa ... wa + n) represents the value of zero, i.e., that all its words are zero.
static inline DECI_UNUSED DECI_FORCE_INLINE
bool deci_is_zero_n(deci_UWORD *wa, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (wa[i])
            return false;
    return true;
}

// Compares (wa ... wa + n) and (wb ... wb + n), returning:
//   * 'if_less' if the former is less than the latter;
//   * 'if_eq' if the former is equal to the latter;
//   * 'if_greater' if the former is greater than the latter.
static inline DECI_UNUSED DECI_FORCE_INLINE
int deci_compare_n(
        deci_UWORD *wa, deci_UWORD *wb, size_t n,
        int if_less, int if_eq, int if_greater)
{
    while (n) {
        --n;
        const deci_UWORD x = wa[n];
        const deci_UWORD y = wb[n];
        if (x != y)
            return x < y ? if_less : if_greater;
    }
    return if_eq;
}

// Returns pointer to the last non-zero word of (wa ... wa_end), or, if there is none, returns 'wa'.
static inline DECI_UNUSED DECI_FORCE_INLINE
deci_UWORD *deci_normalize(deci_UWORD *wa, deci_UWORD *wa_end)
{
    while (wa_end != wa) {
        --wa_end;
        if (*wa_end)
            return ++wa_end;
    }
    return wa_end;
}

// Returns the index of the last non-zero word of (wa ... wa + n), or, if there is none, returns 0.
static inline DECI_UNUSED DECI_FORCE_INLINE
size_t deci_normalize_n(deci_UWORD *wa, size_t n)
{
    while (n) {
        --n;
        if (wa[n])
            return ++n;
    }
    return n;
}

// Returns the index of the first non-zero word of (wa ... wa + n), or, if there is none, returns
// 'n'.
static inline DECI_UNUSED DECI_FORCE_INLINE
size_t deci_skip0_n(deci_UWORD *wa, size_t n)
{
    size_t i = 0;
    while (i != n && wa[i] == 0)
        ++i;
    return i;
}

// Returns pointer to the first non-zero word of (wa ... wa_end), or, if there is none, returns
// 'wa_end'.
static inline DECI_UNUSED DECI_FORCE_INLINE
deci_UWORD *deci_skip0(deci_UWORD *wa, deci_UWORD *wa_end)
{
    while (wa != wa_end && *wa == 0)
        ++wa;
    return wa;
}

// The compiler doesn't know we are not going to copy around/zero out gigabytes of deci_UWORD's,
// so for non-constant sizes it inserts calls to actual 'memset()'/'memcpy()'/'memmove()' functions,
// which are in theory can be more "optimal" than inline loops for very big sizes, but in reality
// the function call itself just trashes the hell out of the cache.
//
// So we provide the "small, dumb and ready to be inlined" versions of the memory manipulation
// functions specifically for 'deci_UWORD' spans.

static inline DECI_UNUSED DECI_FORCE_INLINE
void deci_zero_out(deci_UWORD *wa, deci_UWORD *wa_end)
{
    for (; wa != wa_end; ++wa)
        *wa = 0;
}

static inline DECI_UNUSED DECI_FORCE_INLINE
void deci_zero_out_n(deci_UWORD *wa, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        wa[i] = 0;
}

static inline DECI_UNUSED DECI_FORCE_INLINE
void deci_copy_backward(deci_UWORD *dst, const deci_UWORD *src, size_t n)
{
    while (n) {
        --n;
        dst[n] = src[n];
    }
}

static inline DECI_UNUSED DECI_FORCE_INLINE
void deci_copy_forward(deci_UWORD *dst, const deci_UWORD *src, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = src[i];
}

static inline DECI_UNUSED DECI_FORCE_INLINE
void deci_memcpy(deci_UWORD *dst, const deci_UWORD *src, size_t n)
{
    deci_copy_forward(dst, src, n);
}

static inline DECI_UNUSED DECI_FORCE_INLINE
void deci_memmove(deci_UWORD *dst, const deci_UWORD *src, size_t n)
{
    const uintptr_t dst_i = (uintptr_t) dst;
    const uintptr_t src_i = (uintptr_t) src;
    if (dst_i < src_i)
        deci_copy_forward(dst, src, n);
    else if (dst_i > src_i)
        deci_copy_backward(dst, src, n);
}

#if DECI_DOUBLE_WORD_BITS == 64

#   if defined(__SIZEOF_INT128__) // GNU C (GCC, Clang) or ICC
#       define DECI_HAVE_NATIVE_QUAD_WORD 1
typedef unsigned __int128 deci_QUAD_UWORD;
#   elif defined(_MSC_VER) // MSVC
#       define DECI_HAVE_NATIVE_QUAD_WORD 0
#       include <intrin.h>

typedef struct {
    uint64_t lo;
    uint64_t hi;
} deci_QUAD_UWORD;

#       if defined(_M_X64) || defined(_M_IA64)

#           pragma intrinsic(_umul128)
#           pragma intrinsic(_addcarry_u64)
#           define deci__mul128(A_, B_, Ptr_Hi_)  _umul128(A_, B_, Ptr_Hi_)
#           define deci__addc64(A_, B_, Ptr_Lo_)  _addcarry_u64(0, A_, B_, Ptr_Lo_)
#           if _MSC_VER >= 1920
#               pragma intrinsic(_udiv128)
#               define deci__div128(N_Hi_, N_Lo_, D_, Ptr_R_)  _udiv128(N_Hi_, N_Lo_, D_, Ptr_R_)
#               define DECI_HAVE__DIV128 1
#           else
#               define DECI_HAVE__DIV128 0
#           endif

#       elif defined(_M_ARM64)

#           pragma intrinsic(__umulh)
#           define DECI_HAVE__DIV128 0

static inline DECI_UNUSED DECI_FORCE_INLINE
uint64_t deci__mul128(uint64_t a, uint64_t b, uint64_t *hi)
{
    *hi = __umulh(a, b);
    return a * b;
}

static inline DECI_UNUSED DECI_FORCE_INLINE
unsigned char deci__addc64(uint64_t a, uint64_t b, uint64_t *lo)
{
    uint64_t r = a + b;
    *lo = r;
    return r < a;
}

#       else
#           error "Our list of MSVC-supported 64-bit platforms is not exhaustive; please report."
#       endif

static inline DECI_UNUSED
deci_QUAD_UWORD deci_q_from_3w(deci_UWORD w1, deci_UWORD w2, deci_UWORD w3)
{
    const deci_DOUBLE_UWORD w12 = (w1 * (deci_DOUBLE_UWORD) DECI_BASE) + w2;

    deci_QUAD_UWORD q;
    q.lo = deci__mul128(w12, DECI_BASE, &q.hi);

    const unsigned char carry = deci__addc64(q.lo, w3, &q.lo);
    q.hi += carry;

    return q;
}

static inline DECI_UNUSED
deci_DOUBLE_UWORD deci_q_div_d_to_d(deci_QUAD_UWORD a, deci_DOUBLE_UWORD b)
{
#if DECI_HAVE__DIV128
    uint64_t rem;
    return deci__div128(a.hi, a.lo, b, &rem);
#else
    deci_UWORD q = 0;
    for (unsigned i = 1 << 29; i != 0; i >>= 1) {
        const deci_UWORD x = q | i;

        uint64_t hi;
        uint64_t lo = deci__mul128(x, b, &hi);

        if (hi < a.hi || (hi == a.hi && lo <= a.lo))
            q = x;
    }
    return q;
#endif
}

#   else
#       error "Unsupported compiler."
#   endif

#elif DECI_DOUBLE_WORD_BITS == 32
#           define DECI_HAVE_NATIVE_QUAD_WORD 1
typedef uint64_t deci_QUAD_UWORD;

#else
#   error "BUG: unexpected value of DECI_DOUBLE_WORD_BITS."
#endif

#if DECI_HAVE_NATIVE_QUAD_WORD
static inline DECI_UNUSED DECI_FORCE_INLINE
deci_QUAD_UWORD deci_q_from_3w(deci_UWORD w1, deci_UWORD w2, deci_UWORD w3)
{
    const deci_DOUBLE_UWORD w12 = (w1 * (deci_DOUBLE_UWORD) DECI_BASE) + w2;
    return (w12 * (deci_QUAD_UWORD) DECI_BASE) + w3;
}

static inline DECI_UNUSED DECI_FORCE_INLINE
deci_DOUBLE_UWORD deci_q_div_d_to_d(deci_QUAD_UWORD a, deci_DOUBLE_UWORD b)
{
    return a / b;
}
#endif
