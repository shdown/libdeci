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

#define DECI_CUSTOM_QUAD 1
#include "deci.h"
#include <intrin.h>
#pragma intrinsic(_umul128)
#pragma intrinsic(_addcarry_u64)

#if ! defined(DECI_HAVE_MSVC_UDIV128)
#   if _MSC_VER >= 1920
#       pragma intrinsic(_udiv128)
#       define DECI_HAVE_MSVC_UDIV128 1
#   else
#       define DECI_HAVE_MSVC_UDIV128 0
#   endif
#endif

typedef struct {
    unsigned __int64 lo;
    unsigned __int64 hi;
} deci_QUAD_UWORD;

static inline
deci_QUAD_UWORD deci_q_from_3w(deci_UWORD w1, deci_UWORD w2, deci_UWORD w3)
{
    const deci_DOUBLE_UWORD w12 = (w1 * (deci_DOUBLE_UWORD) DECI_BASE) + w2;

    deci_QUAD_UWORD q;
    q.lo = _umul128(w12, DECI_BASE, &q.hi);

    const unsigned char carry = _addcarry_u64(0, q.lo, w3, &q.lo);
    q.hi += carry;

    return q;
}

static inline
deci_DOUBLE_UWORD deci_q_div_d_to_d(deci_QUAD_UWORD q, deci_DOUBLE_UWORD b)
{
#if DECI_HAVE_MSVC_UDIV128
    unsigned __int64 rem;
    return _udiv128(q.hi, q.lo, b, &rem);
#else
    deci_UWORD lbound = 0;
    deci_UWORD rbound = DECI_BASE;
    while (rbound - lbound > 1) {
        const deci_UWORD mid = (lbound + rbound) / 2;

        unsigned __int64 hi;
        unsigned __int64 lo = _umul128(mid, b, &hi);

        if (hi > q.hi || (hi == q.hi && lo > q.lo))
            rbound = mid;
        else
            lbound = mid;
    }
    return lbound;
#endif
}
