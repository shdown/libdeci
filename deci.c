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

#include "deci.h"
#include "deci_quad_arith.h"

#if !defined(DECI_HAVE_DUMB_COMPILER)
#   define DECI_HAVE_DUMB_COMPILER 1
#endif

#define SWAP(Type_, X_, Y_) \
    do { \
        Type_ swap_tmp__ = (X_); \
        (X_) = (Y_); \
        (Y_) = swap_tmp__; \
    } while (0)

typedef deci_UWORD CARRY;
typedef deci_UWORD BORROW;
#define CARRY_TO_1BIT(X_)  (-(deci_UWORD) (X_))
#define BORROW_TO_1BIT(X_) (-(deci_UWORD) (X_))

// add with carry
static inline DECI_FORCE_INLINE
CARRY adc(deci_UWORD *a, deci_UWORD b, CARRY carry)
{
    const deci_UWORD x = *a + b + CARRY_TO_1BIT(carry);
    const deci_SWORD d = x - DECI_BASE;
#if DECI_HAVE_DUMB_COMPILER
    // 'result' is minus one if (d >= 0), zero otherwise.
    const deci_SWORD result = (~d) >> (deci_SWORD) (DECI_WORD_BITS - 1);
    *a = x - (DECI_BASE & result);
    return result;
#else
    if (d >= 0) {
        *a = d;
        return -1;
    } else {
        *a = x;
        return 0;
    }
#endif
}

// subtract with borrow
static inline DECI_FORCE_INLINE
BORROW sbb(deci_UWORD *a, deci_UWORD b, BORROW borrow)
{
    const deci_SWORD d = *a - b - BORROW_TO_1BIT(borrow);
#if DECI_HAVE_DUMB_COMPILER
    // 'result' is minus one if (d < 0), zero otherwise.
    const deci_SWORD result = d >> (deci_SWORD) (DECI_WORD_BITS - 1);
    *a = d + (DECI_BASE & result);
    return result;
#else
    if (d < 0) {
        *a = d + DECI_BASE;
        return -1;
    } else {
        *a = d;
        return 0;
    }
#endif
}

bool deci_add(
        deci_UWORD *wa, deci_UWORD *wa_end,
        deci_UWORD *wb, deci_UWORD *wb_end)
{
    CARRY carry = 0;
    for (; wb != wb_end; ++wb, ++wa)
        carry = adc(wa, *wb, carry);

    if (!carry)
        return false;

    for (; wa != wa_end; ++wa) {
        if (*wa != DECI_BASE - 1) {
            ++*wa;
            return false;
        }
        *wa = 0;
    }
    return true;
}

bool deci_sub_raw(
        deci_UWORD *wa, deci_UWORD *wa_end,
        deci_UWORD *wb, deci_UWORD *wb_end)
{
    BORROW borrow = 0;
    for (; wb != wb_end; ++wb, ++wa)
        borrow = sbb(wa, *wb, borrow);

    if (!borrow)
        return false;

    for (; wa != wa_end; ++wa) {
        if (*wa) {
            --*wa;
            return false;
        }
        *wa = DECI_BASE - 1;
    }
    return true;
}

void deci_uncomplement(deci_UWORD *wa, deci_UWORD *wa_end)
{
    for (; *wa == 0; ++wa)
        ;
    *wa = DECI_BASE - *wa;
    ++wa;
    for (; wa != wa_end; ++wa)
        *wa = DECI_BASE - 1 - *wa;
}

// Adds ((wa ... wa_end) times 'b') to (out ... implied_out_end), where 'implied_out_end' is not
// actually needed for addition and thus not passed.
//
// Assumes (wa != wa_end).
static void long_mul_round(deci_UWORD *wa, deci_UWORD *wa_end, deci_UWORD b, deci_UWORD *out)
{
    deci_UWORD mul_carry = 0;
    CARRY add_carry = 0;

    do {
        const deci_DOUBLE_UWORD x = *wa * ((deci_DOUBLE_UWORD) b) + mul_carry;

        const deci_UWORD w = x % DECI_BASE;

        mul_carry = x / DECI_BASE;

        add_carry = adc(out, w, add_carry);
        ++out;
    } while (++wa != wa_end);

    if (mul_carry) {
        add_carry = adc(out, mul_carry, add_carry);
        ++out;
    }

    if (add_carry) {
        for (; *out == DECI_BASE - 1; ++out)
            *out = 0;
        ++*out;
    }
}

void deci_mul(
        deci_UWORD *wa, deci_UWORD *wa_end,
        deci_UWORD *wb, deci_UWORD *wb_end,
        deci_UWORD *out)
{
    // Our loop is optimized for long 'a' and short 'b', so swap if 'a' is shorter.
    if ((wa_end - wa) < (wb_end - wb)) {
        SWAP(deci_UWORD *, wa, wb);
        SWAP(deci_UWORD *, wa_end, wb_end);
    }
    for (; wb != wb_end; ++wb, ++out)
        // Safe to call: 'wa' != 'wa_end' because length(a) >= length(b) > 0.
        long_mul_round(wa, wa_end, *wb, out);
}

// ---------------------------------------------------------------------------------------
// For more info on the long division algorithm we use, see:
//  * Knuth section 4.3.1 algorithm D
//  * https://skanthak.homepage.t-online.de/division.html
//  * https://surface.syr.edu/cgi/viewcontent.cgi?article=1162&context=eecs_techreports
// ---------------------------------------------------------------------------------------

// Subtracts from (wx ... wx_end) the value of ('y' times (wz ... wz_end)), modifying the former.
//
// Return the "borrow" word: the word that would have to be subtracted from (*wz_end), if it was
// legal to access.
//
// Assumes 0 <= ((wx_end - wx) - (wz_end - wz)) <= 1.
static deci_UWORD subtract_scaled_raw(
        deci_UWORD *wx, deci_UWORD *wx_end,
        deci_UWORD y,
        deci_UWORD *wz, deci_UWORD *wz_end)
{
    deci_UWORD mul_carry = 0;
    BORROW sub_borrow = 0;

    for (; wz != wz_end; ++wz, ++wx) {
        const deci_DOUBLE_UWORD x = (*wz * (deci_DOUBLE_UWORD) y) + mul_carry;
        const deci_UWORD r = x % DECI_BASE;
        mul_carry = x / DECI_BASE;
        sub_borrow = sbb(wx, r, sub_borrow);
    }

    if (wx == wx_end) {
        return mul_carry + BORROW_TO_1BIT(sub_borrow);
    } else {
        sub_borrow = sbb(wx, mul_carry, sub_borrow);
        return BORROW_TO_1BIT(sub_borrow);
    }
}

static inline DECI_FORCE_INLINE
deci_DOUBLE_UWORD combine(deci_UWORD w1, deci_UWORD w2)
{
    return w1 * ((deci_DOUBLE_UWORD) DECI_BASE) + w2;
}

static deci_UWORD estimate_q(
        deci_UWORD r1, deci_UWORD r2, deci_UWORD r3,
        deci_UWORD b1, deci_UWORD b2)
{
    deci_DOUBLE_UWORD q;

    const deci_DOUBLE_UWORD b12 = combine(b1, b2);
    if (r1 == 0) {
        q = combine(r2, r3) / b12;
    } else {
        const deci_QUAD_UWORD r123 = deci_q_from_3w(r1, r2, r3);
        q = deci_q_div_d_to_d(r123, b12);
    }

    return q < (DECI_BASE - 1) ? q : (DECI_BASE - 1);
}

// Performs a round of long division:
//
// 1. Finds a minimal 'q', 0 <= q < DECI_BASE, such that
//      ((wb ... wb_end) times 'q') is not greater than (wr ... wr_end).
//
// 2. Subtracts ((wb ... wb_end) times 'q') from (wr ... wr_end), modifying the latter.
//
// 3. Returns 'q'.
//
// Assumes that:
//
//  * (wb ... wb_end) is normalized;
//
//  * 0 <= ((wr_end - wr) - (wb_end - wb)) <= 1;
//
//  * the result of the division actually fits into one word: in other words, that
//      ((wb ... wb_end) times 'DECI_BASE') _is_ greater than (wr ... wr_end);
//
//  * (wb_end - wb) >= 2. Note that if it's 1, you should use 'deci_divmod_uword()' or
//      'deci_mod_uword()', and if it's 0, you are dividing by zero -- oops.
static deci_UWORD long_div_round(
        deci_UWORD *wr, deci_UWORD *wr_end,
        deci_UWORD *wb, deci_UWORD *wb_end)
{
    const size_t nwr = wr_end - wr;
    const size_t nwb = wb_end - wb;

    deci_UWORD *aligned_wr_end = wr_end;
    deci_UWORD r1 = 0;
    if (nwr != nwb)
        r1 = *--aligned_wr_end;
    deci_UWORD q = estimate_q(r1, aligned_wr_end[-1], aligned_wr_end[-2], wb_end[-1], wb_end[-2]);

    if (subtract_scaled_raw(wr, wr_end, q, wb, wb_end)) {
        --q;
        (void) deci_add(wr, wr_end, wb, wb_end);
    }

    return q;
}

deci_UWORD deci_divmod_unsafe(
    deci_UWORD *wa, deci_UWORD *wa_end,
    deci_UWORD *wb, deci_UWORD *wb_end)
{
    const size_t nwb = wb_end - wb;

    deci_UWORD *r     = wa_end - nwb;
    deci_UWORD *r_end = wa_end;

    const deci_UWORD qhi = long_div_round(r, r_end, wb, wb_end);

    while (r != wa) {
        --r;
        const deci_UWORD qlo = long_div_round(r, r_end, wb, wb_end);
        *--r_end = qlo;
    }
    return qhi;
}

size_t deci_div(
    deci_UWORD *wa, deci_UWORD *wa_end,
    deci_UWORD *wb, deci_UWORD *wb_end)
{
    wa_end = deci_normalize(wa, wa_end);
    wb_end = deci_normalize(wb, wb_end);

    const size_t nwa = wa_end - wa;
    const size_t nwb = wb_end - wb;

    if (nwa < nwb)
        return 0;

    if (nwb == 1) {
        (void) deci_divmod_uword(wa, wa_end, *wb);
        return nwa;
    }

    const deci_UWORD qhi = deci_divmod_unsafe(wa, wa_end, wb, wb_end);
    const size_t delta = nwa - nwb;
    deci_memmove(wa, wa + nwb, delta);
    wa[delta] = qhi;
    return delta + 1;
}

size_t deci_mod(
    deci_UWORD *wa, deci_UWORD *wa_end,
    deci_UWORD *wb, deci_UWORD *wb_end)
{
    wa_end = deci_normalize(wa, wa_end);
    wb_end = deci_normalize(wb, wb_end);

    const size_t nwa = wa_end - wa;
    const size_t nwb = wb_end - wb;

    if (nwa < nwb)
        return nwa;

    if (nwb == 1) {
        *wa = deci_mod_uword(wa, wa_end, *wb);
        return 1;
    }

    (void) deci_divmod_unsafe(wa, wa_end, wb, wb_end);
    return nwb;
}

deci_UWORD deci_tobits_round(deci_UWORD *wa, deci_UWORD *wa_end)
{
    deci_UWORD carry = 0;

    while (wa_end != wa) {
        --wa_end;
        const deci_DOUBLE_UWORD x = combine(carry, *wa_end);
        *wa_end = x >> DECI_WORD_BITS;
        carry = x;
    }

    return carry;
}

void deci_tolong(deci_UWORD *wa, deci_UWORD *wa_end, deci_DOUBLE_UWORD *out)
{
    const size_t nwa = wa_end - wa;
    if (nwa % 2) {
        --wa_end;
        out[nwa / 2] = *wa_end;
    }

    while (wa != wa_end) {
        const deci_UWORD lo = *wa++;
        const deci_UWORD hi = *wa++;
        *out++ = combine(hi, lo);
    }
}

deci_DOUBLE_UWORD deci_long_tobits_round(deci_DOUBLE_UWORD *wd, deci_DOUBLE_UWORD *wd_end)
{
    deci_DOUBLE_UWORD carry = 0;

    while (wd_end != wd) {
        --wd_end;
        const deci_QUAD_UWORD x = deci_q_from_2w_2w(carry, *wd_end);
        *wd_end = deci_q_bin_hi_2w(x);
        carry = deci_q_bin_lo_2w(x);
    }

    return carry;
}

deci_UWORD deci_frombits_round(deci_UWORD *wa, deci_UWORD *wa_end)
{
    deci_UWORD carry = 0;

    for (; wa != wa_end; ++wa) {
        const deci_DOUBLE_UWORD x = (((deci_DOUBLE_UWORD) *wa) << DECI_WORD_BITS) | carry;

        *wa = x % DECI_BASE;
        carry = x / DECI_BASE;
    }

    return carry;
}
