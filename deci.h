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

// deci_* functions work on (deci_UWORD*) spans representing unsigned little-endian (see below) big
// integers in base (DECI_BASE).
//
// A (deci_UWORD*) span is a pair of pointers,
//     deci_UWORD *wa
// and
//     deci_UWORD *wa_end,
// also written as (wa ... wa_end), where wa[0], wa[1], ..., wa_end[-1] are defined and are less
// than (DECI_BASE).
//
// It is perfectly fine for a (deci_UWORD*) span to be empty, that is, be represented by
// (ptr ... ptr), for some pointer ptr; such a span represents the value of zero.
//
// In general, a (deci_UWORD*) span of (wa ... wa_end) represents the value of
//
//     sum for each integer i in [0; wa_end - wa):
//         wa[i] * DECI_BASE ^ i,
//
// where '^' means exponentiation, not xor. That's what we mean by "little-endian" -- that (wa[0])
// is the *least* significant digit in base (DECI_BASE), and (wa_end[-1]) is the *most* significant.
// This has nothing to do with the actual endianness of the hardware, which we are independent of.
//
// Now, for normalization. To normalize a (deci_UWORD*) span means simply to remove the leading
// zeroes, thus possibly decrementing its "right bound" by some amount.
//
// Normalization is not *required*, but obviously everything will work faster if the numbers are
// normalized -- you just reduce the size of the spans that deci_* function work on.
//
// If you think about it, whenever you have some kind of a dynamic array, you usually can pop from
// the back, but not from the front of it. For example, C's realloc() can truncate the buffer from
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

// We *really* want to be able to natively divide 'deci_DOUBLE_UWORD' values, so it has to be
// 64-bit on 64-bit systems, and 32-bit on 32-bit systems.

#if DECI_WE_ARE_64_BIT

typedef uint32_t deci_UWORD;
typedef int32_t  deci_SWORD;
typedef uint64_t deci_DOUBLE_UWORD;
typedef int64_t  deci_DOUBLE_SWORD;
#define DECI_BASE_LOG 9
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
DECI_UNUSED static const deci_UWORD DECI_BASE = 10000;
#define DECI_FOR_EACH_TENPOW(X) \
    X(0, 1) \
    X(1, 10) \
    X(2, 100) \
    X(3, 1000) \

#endif

// Adds two (deci_UWORD*) spans, writing the result into (wa ... wa_end).
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

// Subtracts two (deci_UWORD*) spans, writing the result into (wa ... wa_end).
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

// Multiplies a (deci_UWORD*) span by a deci_UWORD.
//
// Assumes (b < DECI_BASE); otherwise, the behavior is undefined.
//
// Returns the most significant word of the result, writing the rest of the words into
// (wa ... wa_end).
deci_UWORD deci_mul_uword(
        deci_UWORD *wa, deci_UWORD *wa_end,
        deci_UWORD b);

// Multiplies two (deci_UWORD*) spans, writing the result into
//     (out ... out + N),
// where N = (wa_end - wa) + (wb_end - wb).
//
// Assumes 'out' is a pointer to N words, ALL INITIALLY ZEROED OUT; otherwise, the behavior is
// undefined.
void deci_mul(
        deci_UWORD *wa, deci_UWORD *wa_end,
        deci_UWORD *wb, deci_UWORD *wb_end,
        deci_UWORD *out);

// Divides a (deci_UWORD*) span by a deci_UWORD, writing the quotient into (wa ... wa_end), and
// returning the remainder.
//
// Assumes (0 < b < DECI_BASE); otherwise, the behavior is undefined.
deci_UWORD deci_divmod_uword(
        deci_UWORD *wa, deci_UWORD *wa_end,
        deci_UWORD b);

// Returns the remainder of division of a (deci_UWORD*) span by a deci_UWORD.
//
// Assumes (0 < b < DECI_BASE); otherwise, the behavior is undefined.
deci_UWORD deci_mod_uword(
        deci_UWORD *wa, deci_UWORD *wa_end,
        deci_UWORD b);

// Divides one (deci_UWORD*) span by another (deci_UWORD*) span, writing:
//    * the quotient into (wa ... wa_end);
//    * the remainder into (wr_end - N, wr_end), where N = (wa_end - wa).
//
// <WARNING>
//     NOTE THAT THE SIZE OF THE BUFFER **BEFORE** (wr_end) MUST BE
//         (wa_end - wa),
//     NOT
//         (wb_end - wb) !!!
// </WARNING>
//
// The reason for this is that this span is used as a temporary buffer during the division process,
// and the fact that in the end it happens to be the remainder is merely a nice side effect.
//
// In some way, it even make sense: if A % B = R, then we have not only R < B, but also R <= A.
//
// Assumes that (wb ... wb_end) does not represent the value of zero, i.e., that it contains at
// least one non-zero word; otherwise, the behavior is undefined.
void deci_divmod(
        deci_UWORD *wa, deci_UWORD *wa_end,
        deci_UWORD *wb, deci_UWORD *wb_end,
        deci_UWORD *wr_end);

// Calculates the remainder of division of one (deci_UWORD*) span by another (deci_UWORD*) span,
// writing the result into (wa ... wa_end).
//
// Assumes that (wb ... wb_end) does not represent the value of zero, i.e., that it contains at
// least one non-zero word; otherwise, the behavior is undefined.
void deci_mod(
        deci_UWORD *wa, deci_UWORD *wa_end,
        deci_UWORD *wb, deci_UWORD *wb_end);

// Checks if a (deci_UWORD*) span represents the value of zero, i.e., that all its words are zero.
static inline DECI_UNUSED
bool deci_is_zero(deci_UWORD *wa, deci_UWORD *wa_end)
{
    for (; wa != wa_end; ++wa)
        if (*wa)
            return false;
    return true;
}

// Compares two (deci_UWORD*) spans of equal size, namely,
//     (wa ... wa_end)
// and
//     (wb_end - N, wb_end),
// where N = (wa_end - wa).
static inline DECI_UNUSED
int deci_compare(
        deci_UWORD *wa_end, deci_UWORD *wa,
        deci_UWORD *wb_end,
        int if_less, int if_eq, int if_greater)
{
    while (wa_end != wa) {
        const deci_UWORD x = *--wa_end;
        const deci_UWORD y = *--wb_end;
        if (x != y)
            return x < y ? if_less : if_greater;
    }
    return if_eq;
}

// Returns pointer to the last non-zero word of the (deci_UWORD*) span represented by
// (wa ... wa_end), or, if there is none, returns (wa).
static inline DECI_UNUSED
deci_UWORD *deci_normalize(deci_UWORD *wa, deci_UWORD *wa_end)
{
    while (wa_end != wa) {
        --wa_end;
        if (*wa_end)
            return ++wa_end;
    }
    return wa_end;
}

// The compiler doesn't know we are not going to copy around/zero out gigabytes of deci_UWORD's,
// so for non-constant sizes it inserts calls to actual 'memset()'/'memcpy()'/'memmove()' functions,
// which are in theory can be more "optimal" than inline loops for very big sizes, but in reality
// the function call itself just trashes the hell out of the cache.
//
// So let's write the loops manually.

static inline DECI_UNUSED
void deci_zero_out(deci_UWORD *w, size_t n)
{
    while (n) {
        --n;
        w[n] = 0;
    }
}

static inline DECI_UNUSED
void deci_copy_backward(deci_UWORD *dst, const deci_UWORD *src, size_t n)
{
    while (n) {
        --n;
        dst[n] = src[n];
    }
}

static inline DECI_UNUSED
void deci_copy_forward(deci_UWORD *dst, const deci_UWORD *src, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = src[i];
}

static inline DECI_UNUSED
void deci_memcpy(deci_UWORD *dst, const deci_UWORD *src, size_t n)
{
    deci_copy_backward(dst, src, n);
}

static inline DECI_UNUSED
void deci_memmove(deci_UWORD *dst, const deci_UWORD *src, size_t n)
{
    if (((uintptr_t) dst) < ((uintptr_t) src))
        deci_copy_forward(dst, src, n);
    else
        deci_copy_backward(dst, src, n);
}
