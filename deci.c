#include "deci.h"

#define SWAP_UWORD_PTR(X_, Y_) \
    do { \
        deci_UWORD *swap_tmp__ = (X_); \
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
        goto ret_noext;
    for (; wa != wa_end; ++wa) {
        const deci_UWORD x = *wa + 1;
        if (x != DECI_BASE) {
            *wa = x;
            goto ret_noext;
        }
        *wa = 0;
    }
    return true;

ret_noext:
    return false;
}

bool deci_sub(
        deci_UWORD *wa, deci_UWORD *wa_end,
        deci_UWORD *wb, deci_UWORD *wb_end)
{
    deci_UWORD *const wa_orig = wa;

    bool borrow = false;
    for (; wb != wb_end; ++wb, ++wa)
        borrow = sbb(wa, *wb, borrow);

    if (!borrow)
        goto ret_noneg;

    for (; wa != wa_end; ++wa) {
        const deci_UWORD v = *wa;
        if (v) {
            *wa = v - 1;
            goto ret_noneg;
        }
        *wa = DECI_BASE - 1;
    }

    // Uncomplement.
    //
    // Aassuming what we currently have in (wa ... wa_end) is, in big-endian (that is, written the
    // way we humans write it, from the most significant "digit" to the least significant "digit"),
    //   A_1  A_2  A_3  ...  A_n,
    // we need to calculate
    //
    //   001  000  000  000  ...  000
    // -
    //        A_1  A_2  A_3  ...  A_n
    //  ------------------------------
    //
    // Why? Well, at this point, we have the "borrow" flag set after performing the subtracting over
    // all available "digits", so
    //
    //   (1)
    //  000  X_1  X_2  X_3  ...  X_n
    // -
    //  000  Y_1  Y_2  Y_3  ...  Y_n
    //  ------------------------------
    //  000  A_1  A_2  A_3  ...  A_n
    //
    // Let T =
    //    1  000  000  000  ... 000,
    // that is, a single "digit" with value of 1 and n "digits" with value of 0. We have
    //   (X + T) - Y = A,
    // so
    //   Y - X = T - A.
    //
    // In order to calculate that value, we need to:
    //
    //   1. Skip to the least-significant non-zero digit (because subtracting zero from zero just
    //      produces zero, neither altering the value nor requiring borrow). There *has* to be a
    //      non-zero digit somewhere, otherwise the result of the subtraction simply would not
    //      fit into the (wa ... wa_end) range, which is impossible.
    //
    //   2. Borrow one from the higher "digit" in order to subtract non-zero from zero. If we
    //      had a non-zero "digit" with value of 'v', the result is 'DECI_BASE - v'.
    //
    //   3. For each "next" (more significant) "digit", perform subtraction with the borrow of 1:
    //      if we had a "digit" with value of 'v', the result is 'DECI_BASE - 1 - v'.
    wa = wa_orig;
    for (; *wa == 0; ++wa)
        ;
    *wa = DECI_BASE - *wa;
    ++wa;
    for (; wa != wa_end; ++wa)
        *wa = DECI_BASE - 1 - *wa;
    return true;

ret_noneg:
    return false;
}

deci_UWORD deci_mul_uword(deci_UWORD *wa, deci_UWORD *wa_end, deci_UWORD b)
{
    deci_UWORD carry = 0;
    for (; wa != wa_end; ++wa) {
        const deci_DOUBLE_UWORD x = *wa * ((deci_DOUBLE_UWORD) b) + carry;
        *wa = x % DECI_BASE;
        carry = x / DECI_BASE;
    }
    return carry;
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
        SWAP_UWORD_PTR(wa, wb);
        SWAP_UWORD_PTR(wa_end, wb_end);
    }
    for (; wb != wb_end; ++wb, ++out)
        // Safe to call: 'wa' != 'wa_end' because length(a) >= length(b) > 0.
        long_mul_round(wa, wa_end, *wb, out);
}

deci_UWORD deci_divmod_uword(deci_UWORD *wa, deci_UWORD *wa_end, deci_UWORD b)
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

deci_UWORD deci_mod_uword(deci_UWORD *wa, deci_UWORD *wa_end, deci_UWORD b)
{
    deci_UWORD carry = 0;
    while (wa_end != wa) {
        --wa_end;

        const deci_DOUBLE_UWORD x = *wa_end + DECI_BASE * (deci_DOUBLE_UWORD) carry;
        carry = x % b;
    }
    return carry;
}

// Checks if (wx ... wx_end) is less than ('y' times (wz ... wz_end)).
//
// Assumes that (wx ... wx_end) and (wz ... wz_end) are normalized.
//
// Assumes that (wx_end - wx) >= (wz_end - wz). Note that this assumption actually makes some sense:
// otherwise, the result would be (y != 0), not depending on (wx ... wx_end) or (wz ... wz_end) at
// all.
static inline bool x_less_yz(
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
        sub_borrow = *wx < (r + sub_borrow);
    }

    if (mul_carry) {
        if (wx == wx_end)
            goto less;
        sub_borrow = *wx < (mul_carry + sub_borrow);
        ++wx;
    }

    if (!sub_borrow)
        goto not_less;
    for (; wx != wx_end; ++wx)
        if (*wx)
            goto not_less;
less:
    return true;
not_less:
    return false;
}

// Subtracts ('y' times (wz ... wz_end)) from (wx ... wx_end), modifying (wx ... wx_end).
//
// Assumes that (wx ... wx_end) and (wz ... wz_end) are normalized.
//
// Assumes that (wx_end - wx) >= (wz_end - wz).
//
// Assumes !(x_less_yz(wx, wx_end, y, wz, wz_end)), i.e., that the result of subtraction is
// non-negative.
static inline void x_sub_yz(
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

    if (mul_carry) {
        sub_borrow = sbb(wx, mul_carry, sub_borrow);
        ++wx;
    }

    if (sub_borrow) {
        for (; *wx == 0; ++wx)
            *wx = DECI_BASE - 1;
        --*wx;
    }

    (void) wx_end; // unused
}

// Returns the two most significant words of (implied_w ... w_end) combined, that is, the value of
//     hi * DECI_BASE + lo,
// where hi = w_end[-1], lo = w_end[-2].
static inline deci_DOUBLE_UWORD high2(deci_UWORD *w_end)
{
    return (w_end[-1] * (deci_DOUBLE_UWORD) DECI_BASE) + w_end[-2];
}

// Performs a round of long division:
//
// 1. Finds a minimal 'q', 0 <= q < DECI_BASE, such that
//      ((wb ... wb_end) times 'q') is not greater than (wr ... wr_end).
//
// 2. Subtracts ((wb ... wb_end) times 'q') from (wr ... wr_end), modifying (wr ... wr_end).
//
// 3. Returns 'q'.
//
// Assumes that:
//
//  * (wr ... wr_end) and (wb ... wb_end) are normalized.
//
//  * (wb_end - wb) >= 2. Note that if it has length of 1, you should use 'deci_divmod_uword()' or
//     'deci_mod_uword()', and if it has length of 0, you are dividing by zero -- oops.
//
//  * The result of division actually fits into one 'deci_UWORD', that is,
//      ((wb ... wb_end) times 'DECI_BASE') is greater than (wr ... wr_end).
static inline deci_UWORD long_div_round(
        deci_UWORD *wr, deci_UWORD *wr_end,
        deci_UWORD *wb, deci_UWORD *wb_end)
{
    const size_t nwr = wr_end - wr;
    const size_t nwb = wb_end - wb;
    if (nwr < nwb)
        return 0;

    // Two most significant words of (wr ... wr_end).
    //
    // Since (wr_end - wr) >= (wb_end - wb) >= 2, this is OK.
    const deci_DOUBLE_UWORD r_hi = high2(wr_end);

    // If (nwr == nwb), then use two most significant words of (wb ... wb_end).
    //
    // Otherwise, (nwb < nwr) as we have already checked for (nwr < nwb). But if that's the case,
    // then (nwb == nwr - 1) must hold, as the result of the division must fit into one word. So
    // let's just use the value of (wb_end[-1]) and pretend the word corresponding in significance
    // to (wr_end[-1]) was zero.
    const deci_DOUBLE_UWORD b_hi = (nwr == nwb) ? high2(wb_end) : wb_end[-1];

    // So, we are dividing something which has two most significant words of 'r_hi', by something
    // which has two most significant words of 'b_hi'. Using big-endian notation, we are dividing
    //     r_hi  ??  ??  ...  ??
    // by
    //     b_hi  ??  ??  ...  ??.
    //
    // Let's calculate upper- and lower bounds on the quotient.
    const deci_DOUBLE_UWORD rbound_raw = (r_hi + 1) / b_hi + 1;
    deci_UWORD rbound = rbound_raw < DECI_BASE ? rbound_raw : DECI_BASE;
    deci_UWORD lbound = r_hi / (b_hi + 1);

    // Binary search on the quotient.
    // Loop invariant: (lbound <= q < rbound), so our result will be in 'lbound'.
    while (rbound - lbound > 1) {
        const deci_UWORD mid = (lbound + rbound) / 2;
        if (x_less_yz(wr, wr_end, mid, wb, wb_end)) {
            rbound = mid;
        } else {
            lbound = mid;
        }
    }
    x_sub_yz(wr, wr_end, lbound, wb, wb_end);
    return lbound;
}

static void single_word_result(deci_UWORD *wr, deci_UWORD *wr_end, deci_UWORD result)
{
    if (wr != wr_end)
        *wr++ = result;
    for (; wr != wr_end; ++wr)
        *wr = 0;
}

void deci_divmod(
    deci_UWORD *wa, deci_UWORD *wa_end,
    deci_UWORD *wb, deci_UWORD *wb_end,
    deci_UWORD *wr_end)
{
    wb_end = deci_normalize(wb, wb_end);

    if ((wb_end - wb) == 1) {
        const deci_UWORD rem = deci_divmod_uword(wa, wa_end, *wb);
        single_word_result(
            /*wr=*/wr_end - (wa_end - wa),
            /*wr_end=*/wr_end,
            /*result=*/rem);
        return;
    }

    deci_UWORD *wr = wr_end;

    while (wa_end != wa) {
        --wa_end;

        --wr;
        *wr = *wa_end;

        wr_end = deci_normalize(wr, wr_end);
        *wa_end = long_div_round(
            wr, wr_end,
            wb, wb_end);
    }
}

void deci_mod(
    deci_UWORD *wa, deci_UWORD *wa_end,
    deci_UWORD *wb, deci_UWORD *wb_end)
{
    wb_end = deci_normalize(wb, wb_end);

    if ((wb_end - wb) == 1) {
        const deci_UWORD rem = deci_mod_uword(wa, wa_end, *wb);
        single_word_result(
            /*wr=*/wa,
            /*wr_end=*/wa_end,
            /*result=*/rem);
        return;
    }

    deci_UWORD *wa_cur = wa_end;
    while (wa_cur != wa) {
        --wa_cur;

        wa_end = deci_normalize(wa_cur, wa_end);
        (void) long_div_round(
            wa_cur, wa_end,
            wb, wb_end);
    }
}
