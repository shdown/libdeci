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

    // 'result' is minus one if (d >= 0), zero otherwise.
    const deci_SWORD result = (~d) >> (deci_SWORD) (DECI_WORD_BITS - 1);
    *a = x - (DECI_BASE & result);
    return result;
}

// subtract with borrow
static inline DECI_FORCE_INLINE
BORROW sbb(deci_UWORD *a, deci_UWORD b, BORROW borrow)
{
    const deci_SWORD d = *a - b - BORROW_TO_1BIT(borrow);

    // 'result' is minus one if (d < 0), zero otherwise.
    const deci_SWORD result = d >> (deci_SWORD) (DECI_WORD_BITS - 1);
    *a = d + (DECI_BASE & result);
    return result;
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

void deci_add_scaled(
        deci_UWORD *wx,
        deci_UWORD y,
        deci_UWORD *wz, deci_UWORD *wz_end)
{
    deci_UWORD mul_carry = 0;
    CARRY add_carry = 0;

    for (; wz != wz_end; ++wz) {
        const deci_DOUBLE_UWORD x = *wz * ((deci_DOUBLE_UWORD) y) + mul_carry;

        const deci_UWORD w = x % DECI_BASE;

        mul_carry = x / DECI_BASE;

        add_carry = adc(wx, w, add_carry);
        ++wx;
    }

    if (mul_carry) {
        add_carry = adc(wx, mul_carry, add_carry);
        ++wx;
    }

    if (add_carry) {
        for (; *wx == DECI_BASE - 1; ++wx)
            *wx = 0;
        ++*wx;
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
        deci_add_scaled(out, *wb, wa, wa_end);
}

// ---------------------------------------------------------------------------------------
// For more info on the long division algorithm we use, see:
//  * Knuth section 4.3.1 algorithm D
//  * https://skanthak.homepage.t-online.de/division.html
//  * https://surface.syr.edu/cgi/viewcontent.cgi?article=1162&context=eecs_techreports
// ---------------------------------------------------------------------------------------

deci_UWORD deci_sub_scaled_raw(
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

    if (wx == wx_end)
        return mul_carry + BORROW_TO_1BIT(sub_borrow);

    sub_borrow = sbb(wx, mul_carry, sub_borrow);
    ++wx;

    if (!sub_borrow)
        return 0;
    for (; wx != wx_end; ++wx) {
        if (*wx) {
            --*wx;
            return 0;
        }
        *wx = DECI_BASE - 1;
    }
    return 1;
}

static inline DECI_FORCE_INLINE
deci_DOUBLE_UWORD combine(deci_UWORD w1, deci_UWORD w2)
{
    return w1 * ((deci_DOUBLE_UWORD) DECI_BASE) + w2;
}

static inline DECI_FORCE_INLINE
deci_QUAD_UWORD combine_to_quad(deci_DOUBLE_UWORD x1, deci_DOUBLE_UWORD x2)
{
    return x1 * ((deci_QUAD_UWORD) DECI_BASE) * DECI_BASE + x2;
}

static inline deci_UWORD estimate_quotient(
        deci_UWORD r1,
        deci_DOUBLE_UWORD r23,
        deci_DOUBLE_UWORD b12)
{
    deci_DOUBLE_UWORD q;
    if (r1 == 0) {
        q = r23 / b12;
    } else {
        q = combine_to_quad(r1, r23) / b12;
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
static inline deci_UWORD long_div_round(
        deci_UWORD *wr, deci_UWORD *wr_end,
        deci_UWORD *wb, deci_UWORD *wb_end,
        deci_DOUBLE_UWORD b12)
{
    const size_t nwr = wr_end - wr;
    const size_t nwb = wb_end - wb;
    deci_UWORD q;
    if (nwr == nwb) {
        q = estimate_quotient(
            /*r1=*/0,
            /*r23=*/combine(wr_end[-1], wr_end[-2]),
            /*b12=*/b12);
    } else {
        q = estimate_quotient(
            /*r1=*/wr_end[-1],
            /*r23=*/combine(wr_end[-2], wr_end[-3]),
            /*b12=*/b12);
    }

    if (deci_sub_scaled_raw(wr, wr_end, q, wb, wb_end)) {
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

    const deci_DOUBLE_UWORD b12 = combine(wb_end[-1], wb_end[-2]);

    deci_UWORD *r     = wa_end - nwb;
    deci_UWORD *r_end = wa_end;

    const deci_UWORD qhi = long_div_round(r, r_end, wb, wb_end, b12);

    while (r != wa) {
        --r;
        const deci_UWORD qlo = long_div_round(r, r_end, wb, wb_end, b12);
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
        deci_QUAD_UWORD x = combine_to_quad(carry, *wd_end);
        *wd_end = x >> DECI_DOUBLE_WORD_BITS;
        carry = (deci_DOUBLE_UWORD) x;
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
