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

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include "../deci.h"

#define SWAP(Type, X, Y) \
    do { \
        Type swap_tmp__ = (X); \
        (X) = (Y); \
        (Y) = swap_tmp__; \
    } while (0)

typedef struct {
    size_t size;
    deci_UWORD words[];
} BigInt;

static void oom_handler(void)
{
    fputs("Out of memory.\n", stderr);
    abort();
}

static size_t x_add_zu(size_t a, size_t b)
{
    if (a > SIZE_MAX - b)
        oom_handler();
    return a + b;
}

static size_t x_mul_zu(size_t a, size_t b)
{
    if (b && a > SIZE_MAX / b)
        oom_handler();
    return a * b;
}

static void *x_realloc(void *p, size_t a, size_t b, size_t c)
{
    const size_t n = x_add_zu(a, x_mul_zu(b, c));
    p = realloc(p, n);
    if (n && !p)
        oom_handler();
    return p;
}

static inline size_t normalize_dword_n(deci_DOUBLE_UWORD *wd, size_t nwd)
{
    while (nwd && wd[nwd - 1] == 0)
        --nwd;
    return nwd;
}

static BigInt *bigint_realloc(BigInt *b, size_t new_size)
{
    b = x_realloc(b, sizeof(BigInt), sizeof(deci_UWORD), new_size);
    b->size = new_size;
    return b;
}

static inline BigInt *bigint_alloc(size_t size)
{
    return bigint_realloc(NULL, size);
}

static BigInt *bigint_alloc0(size_t size)
{
    BigInt *b = bigint_realloc(NULL, size);
    deci_zero_out_n(b->words, size);
    return b;
}

static BigInt *bigint_push_word(BigInt *b, deci_UWORD w)
{
    const size_t old_size = b->size;
    b = bigint_realloc(b, old_size + 1);
    b->words[old_size] = w;
    return b;
}

static inline void bigint_free(BigInt *b)
{
    free(b);
}

static char *x_read_line(void)
{
    char *buf = NULL;
    size_t nbuf = 0;
    ssize_t r = getline(&buf, &nbuf, stdin);
    if (r < 0) {
        if (feof(stdin)) {
            fprintf(stderr, "Unexpected EOF.\n");
        } else {
            perror("getline");
        }
        abort();
    }
    if (r && buf[r - 1] == '\n')
        buf[r - 1] = '\0';
    return buf;
}

static deci_UWORD x_parse_word(const char *s, const char *s_end)
{
    deci_UWORD w = 0;
    for (; s != s_end; ++s) {
        const int digit = *s - '0';
        if (digit < 0 || digit > 9) {
            fprintf(stderr, "Expected digit, found '%c'\n", *s);
            abort();
        }
        w *= 10;
        w += digit;
    }
    return w;
}

static BigInt *x_parse_span(const char *s, const char *s_end)
{
    while (s != s_end && *s == '0')
        ++s;

    const size_t ns = s_end - s;
    const size_t nwords = (ns / DECI_BASE_LOG) + !!(ns % DECI_BASE_LOG);

    BigInt *b = bigint_alloc(nwords);

    deci_UWORD *out = b->words;
    size_t i = ns;
    for (; i >= DECI_BASE_LOG; i -= DECI_BASE_LOG) {
        *out++ = x_parse_word(s + i - DECI_BASE_LOG, s + i);
    }
    if (i) {
        *out++ = x_parse_word(s, s + i);
    }
    return b;
}

static BigInt *x_read_bigint(void)
{
    char *s = x_read_line();
    const size_t ns = strlen(s);

    if (ns == 0) {
        fprintf(stderr, "Expected number, found empty line.\n");
        abort();
    }

    BigInt *b = x_parse_span(s, s + ns);

    free(s);
    return b;
}

static deci_UWORD x_read_word(void)
{
    char *s = x_read_line();
    const size_t ns = strlen(s);

    if (ns == 0 || ns > DECI_BASE_LOG) {
        fprintf(stderr, "Expected single-word number, found line of length %zu.\n", ns);
        abort();
    }

    deci_UWORD w = x_parse_word(s, s + ns);
    free(s);
    return w;
}

static void write_word(deci_UWORD w)
{
    printf("%llu\n", (unsigned long long) w);
}

static void write_dword(deci_DOUBLE_UWORD dw)
{
    printf("%llu\n", (unsigned long long) dw);
}

static void write_span(deci_UWORD *w, size_t n, bool negative)
{
    n = deci_normalize_n(w, n);

    if (!n) {
        printf("0\n");
        return;
    }

    if (negative)
        printf("-");

    --n;
    printf("%llu", (unsigned long long) w[n]);
    while (n) {
        --n;
        printf("%0*llu", (int) DECI_BASE_LOG, (unsigned long long) w[n]);
    }
    printf("\n");
}

static void write_bigint(BigInt *b, bool negative)
{
    write_span(b->words, b->size, negative);
}

static void check_divisor_word(deci_UWORD w)
{
    if (!w) {
        fprintf(stderr, "Division by zero.\n");
        abort();
    }
}

static void check_divisor(BigInt *b, size_t min_size)
{
    if (b->size < min_size) {
        fprintf(stderr, "Division by %zu-word number (expected at least %zu).\n",
                b->size, min_size);
        abort();
    }
}

static bool interact(void)
{
    char *action = x_read_line();
    switch (action[0]) {
    case '+':
        {
            BigInt *a = x_read_bigint();
            BigInt *b = x_read_bigint();

            if (a->size < b->size) {
                SWAP(BigInt *, a, b);
            }

            if (deci_add(
                    a->words, a->words + a->size,
                    b->words, b->words + b->size))
            {
                a = bigint_push_word(a, 1);
            }

            write_bigint(a, false);

            bigint_free(a);
            bigint_free(b);
        }
        break;
    case '-':
        {
            BigInt *a = x_read_bigint();
            BigInt *b = x_read_bigint();

            bool neg = false;

            if (a->size < b->size) {
                SWAP(BigInt *, a, b);
                neg = true;
            }

            neg ^= deci_sub(
                a->words, a->words + a->size,
                b->words, b->words + b->size);
            a->size = deci_normalize_n(a->words, a->size);

            write_bigint(a, neg);

            bigint_free(a);
            bigint_free(b);
        }
        break;
    case '1':
        switch (action[1]) {
        case '*':
            {
                BigInt *a = x_read_bigint();
                deci_UWORD b = x_read_word();

                deci_UWORD hi = deci_mul_uword(a->words, a->words + a->size, b);
                a = bigint_push_word(a, hi);
                a->size = deci_normalize_n(a->words, a->size);

                write_bigint(a, false);

                bigint_free(a);
            }
            break;
        case 'd':
            {
                BigInt *a = x_read_bigint();
                deci_UWORD b = x_read_word();

                check_divisor_word(b);
                deci_UWORD m = deci_divmod_uword(a->words, a->words + a->size, b);
                a->size = deci_normalize_n(a->words, a->size);

                write_bigint(a, false);
                write_word(m);

                bigint_free(a);
            }
            break;
        case '%':
            {
                BigInt *a = x_read_bigint();
                deci_UWORD b = x_read_word();

                check_divisor_word(b);
                deci_UWORD m = deci_mod_uword(a->words, a->words + a->size, b);

                write_word(m);

                bigint_free(a);
            }
            break;
        default:
            fprintf(stderr, "First line starts with invalid sequence: '1%c'\n", action[1]);
            return false;
        }
        break;
    case '*':
        {
            BigInt *a = x_read_bigint();
            BigInt *b = x_read_bigint();
            const size_t nr = x_add_zu(a->size, b->size);
            BigInt *r = bigint_alloc0(nr);

            deci_mul(
                a->words, a->words + a->size,
                b->words, b->words + b->size,
                r->words);
            r->size = deci_normalize_n(r->words, nr);

            write_bigint(r, false);

            bigint_free(a);
            bigint_free(b);
            bigint_free(r);
        }
        break;
    case 'd':
        {
            BigInt *a = x_read_bigint();
            BigInt *b = x_read_bigint();
            check_divisor(b, 2);

            if (a->size < b->size) {
                // quotient
                write_span(NULL, 0, false);
                // remainder
                write_bigint(a, false);

            } else {
                deci_UWORD qhi = deci_divmod_unsafe(
                    a->words, a->words + a->size,
                    b->words, b->words + b->size);

                a = bigint_push_word(a, qhi);

                // quotient
                write_span(
                    a->words + b->size,
                    a->size - b->size,
                    false);

                // remainder
                write_span(
                    a->words,
                    b->size,
                    false);
            }

            bigint_free(a);
            bigint_free(b);
        }
        break;
    case '/':
        {
            BigInt *a = x_read_bigint();
            BigInt *b = x_read_bigint();
            check_divisor(b, 1);

            const size_t nr = deci_div(
                a->words, a->words + a->size,
                b->words, b->words + b->size);
            a->size = deci_normalize_n(a->words, nr);

            write_bigint(a, false);

            bigint_free(a);
            bigint_free(b);
        }
        break;
    case '%':
        {
            BigInt *a = x_read_bigint();
            BigInt *b = x_read_bigint();
            check_divisor(b, 1);

            const size_t nr = deci_mod(
                a->words, a->words + a->size,
                b->words, b->words + b->size);
            a->size = deci_normalize_n(a->words, nr);

            write_bigint(a, false);

            bigint_free(a);
            bigint_free(b);
        }
        break;
    case '?':
        {
            BigInt *a = x_read_bigint();
            BigInt *b = x_read_bigint();

            char c;
            if (a->size != b->size) {
                c = a->size < b->size ? '<' : '>';
            } else {
                c = deci_compare_n(
                    a->words, b->words, a->size,
                    '<', '=', '>');
            }

            printf("%c\n", c);

            bigint_free(a);
            bigint_free(b);
        }
        break;
    case 't':
        {
            BigInt *a = x_read_bigint();

            do {
                deci_UWORD lo = deci_tobits_round(a->words, a->words + a->size);
                write_word(lo);
                a->size = deci_normalize_n(a->words, a->size);
            } while (a->size);

            bigint_free(a);
        }
        break;
    case 'T':
        {
            BigInt *a = x_read_bigint();
            const size_t na = a->size;
            size_t nd = (na / 2) + (na % 2);
            deci_DOUBLE_UWORD *wd = x_realloc(NULL, 0, sizeof(deci_DOUBLE_UWORD), nd);

            deci_tolong(a->words, a->words + na, wd);
            do {
                deci_DOUBLE_UWORD lo = deci_long_tobits_round(wd, wd + nd);
                write_dword(lo);
                nd = normalize_dword_n(wd, nd);
            } while (nd);

            free(wd);
            bigint_free(a);
        }
        break;
    case 'f':
        {
            BigInt *a = x_read_bigint();

            deci_UWORD nrounds = x_read_word();
            for (; nrounds; --nrounds) {
                deci_UWORD hi = deci_frombits_round(a->words, a->words + a->size);
                write_word(hi);
            }

            bigint_free(a);
        }
        break;
    default:
        fprintf(stderr, "First line starts with invalid symbol: '%c'\n", action[0]);
        return false;
    }

    free(action);
    return true;
}

static void print_usage(const char *me)
{
    if (!me)
        me = "driver";
    fprintf(stderr, "USAGE: %s wordbits\n", me);
    fprintf(stderr, "       %s interact\n", me);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Expected exactly one position argument.\n");
        print_usage(argv[0]);
        return 2;
    }
    const char *arg = argv[1];
    if (strcmp(arg, "wordbits") == 0) {
        printf("%d\n", (int) DECI_WORD_BITS);
        return 0;
    } else if (strcmp(arg, "interact") == 0) {
        return interact() ? 0 : 1;
    } else {
        fprintf(stderr, "Invalid argument: '%s'.\n", arg);
        print_usage(argv[0]);
        return 2;
    }
}
