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

#include "deci.h"

#if DECI_DOUBLE_WORD_BITS == 64

#   if defined(__SIZEOF_INT128__) || defined(__INTEL_COMPILER)
// Either GCC, Clang or ICC; all support '__int128' on 64-bit platforms.
#       define DECI_HAVE_NATIVE_QUAD_WORD 1
typedef unsigned __int128 deci_QUAD_UWORD;

#   elif defined(_MSC_VER)
// MSVC; various intrinsics may be available depending on arch.
#       define DECI_HAVE_NATIVE_QUAD_WORD 0
#       include <intrin.h>

typedef struct {
    uint64_t lo;
    uint64_t hi;
} deci_QUAD_UWORD;

#       if defined(_M_X64) || defined(_M_IA64)

#           pragma intrinsic(_umul128)
#           pragma intrinsic(_addcarry_u64)
#           define deci__mul128(A, B, Ptr_Hi)  _umul128(A, B, Ptr_Hi)
#           define deci__addc64(A, B, Ptr_Lo)  _addcarry_u64(0, A, B, Ptr_Lo)
#           if _MSC_VER >= 1920
#               pragma intrinsic(_udiv128)
#               define deci__div128(A_Hi, A_Lo, B, Ptr_R)  _udiv128(A_Hi, A_Lo, B, Ptr_R)
#               define DECI_HAVE__DIV128 1
#           else
#               define DECI_HAVE__DIV128 0
#           endif

#       elif defined(_M_ARM64)

#           pragma intrinsic(__umulh)
#           define DECI_HAVE__DIV128 0

static inline DECI_UNUSED DECI_FORCE_INLINE
uint64_t deci__mul128(uint64_t a, uint64_t b, uint64_t *hi) { *hi = __umulh(a, b); return a * b; }

static inline DECI_UNUSED DECI_FORCE_INLINE
unsigned char deci__addc64(uint64_t a, uint64_t b, uint64_t *lo) { return (*lo = a + b) < a; }

#       else
#           error "Our list of MSVC-supported 64-bit platforms is not exhaustive; please report."
#       endif

// Implementation of 'deci_q_*' functions for 64-bit MSVC.

static inline DECI_UNUSED DECI_FORCE_INLINE
deci_QUAD_UWORD deci_q_from_2w_2w(deci_DOUBLE_UWORD hi, deci_DOUBLE_UWORD lo)
{
    deci_QUAD_UWORD q;
    q.lo = deci__mul128(
        hi,
        ((deci_DOUBLE_UWORD) DECI_BASE) * DECI_BASE,
        &q.hi);

    unsigned char carry = deci__addc64(q.lo, lo, &q.lo);
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

static inline DECI_UNUSED DECI_FORCE_INLINE
deci_DOUBLE_UWORD deci_q_bin_hi_2w(deci_QUAD_UWORD q)
{
    return q.hi;
}

static inline DECI_UNUSED DECI_FORCE_INLINE
deci_DOUBLE_UWORD deci_q_bin_lo_2w(deci_QUAD_UWORD q)
{
    return q.lo;
}

#   else
// Neither of: GCC, Clang, ICC, MSVC.
#       error "Unsupported compiler."
#   endif

#elif DECI_DOUBLE_WORD_BITS == 32

#   define DECI_HAVE_NATIVE_QUAD_WORD 1
typedef uint64_t deci_QUAD_UWORD;

#else
#   error "BUG: unexpected value of DECI_DOUBLE_WORD_BITS."
#endif

#if DECI_HAVE_NATIVE_QUAD_WORD

// Implementation of 'deci_q_*' functions for the case when 'deci_QUAD_UWORD' is "native", primitive
// integral type.

static inline DECI_UNUSED DECI_FORCE_INLINE
deci_QUAD_UWORD deci_q_from_2w_2w(deci_DOUBLE_UWORD hi, deci_DOUBLE_UWORD lo)
{
    return ((deci_QUAD_UWORD) hi) * (((deci_DOUBLE_UWORD) DECI_BASE) * DECI_BASE) + lo;
}

static inline DECI_UNUSED DECI_FORCE_INLINE
deci_DOUBLE_UWORD deci_q_div_d_to_d(deci_QUAD_UWORD a, deci_DOUBLE_UWORD b)
{
    return a / b;
}

static inline DECI_UNUSED DECI_FORCE_INLINE
deci_DOUBLE_UWORD deci_q_bin_hi_2w(deci_QUAD_UWORD q)
{
    return q >> DECI_DOUBLE_WORD_BITS;
}

static inline DECI_UNUSED DECI_FORCE_INLINE
deci_DOUBLE_UWORD deci_q_bin_lo_2w(deci_QUAD_UWORD q)
{
    return q;
}
#endif