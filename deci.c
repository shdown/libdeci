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

#define SWAP(Type_, X_, Y_) \
    do { \
        Type_ swap_tmp__ = (X_); \
        (X_) = (Y_); \
        (Y_) = swap_tmp__; \
    } while (0)

// add with carry
static inline bool adc(deci_UWORD *a, deci_UWORD b, bool carry)
{
    const deci_UWORD x = *a + b + carry;
    const deci_SWORD d = x - DECI_BASE;
    if (d >= 0) {
        *a = d;
        return true;
    } else {
        *a = x;
        return false;
    }
}

// subtract with borrow
static inline bool sbb(deci_UWORD *a, deci_UWORD b, bool borrow)
{
    const deci_SWORD d = *a - b - borrow;
    if (d < 0) {
        *a = d + DECI_BASE;
        return true;
    } else {
        *a = d;
        return false;
    }
}

bool deci_add(
        deci_UWORD *wa, deci_UWORD *wa_end,
        deci_UWORD *wb, deci_UWORD *wb_end)
{
    bool carry = false;
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
    bool borrow = false;
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
    bool add_carry = false;

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
    bool sub_borrow = false;

    for (; wz != wz_end; ++wz, ++wx) {
        const deci_DOUBLE_UWORD x = (*wz * (deci_DOUBLE_UWORD) y) + mul_carry;
        const deci_UWORD r = x % DECI_BASE;
        mul_carry = x / DECI_BASE;
        sub_borrow = sbb(wx, r, sub_borrow);
    }

    if (wx == wx_end) {
        return mul_carry + sub_borrow;
    } else {
        return sbb(wx, mul_carry, sub_borrow);
    }
}

static deci_UWORD estimate_q(
        deci_UWORD r1, deci_UWORD r2, deci_UWORD r3,
        deci_UWORD b1, deci_UWORD b2)
{
    const deci_QUAD_UWORD r123 = deci_q_from_3w(r1, r2, r3);

    const deci_DOUBLE_UWORD b12 = (b1 * (deci_DOUBLE_UWORD) DECI_BASE) + b2;

    const deci_DOUBLE_UWORD q = deci_q_div_d_to_d(r123, b12);

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
    deci_UWORD r_hi = 0;
    if (nwr != nwb)
        r_hi = *--aligned_wr_end;
    deci_UWORD q = estimate_q(
        r_hi,
        aligned_wr_end[-1],
        aligned_wr_end[-2],
        wb_end[-1],
        wb_end[-2]);

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
