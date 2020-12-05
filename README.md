**libdeci** is a big-decimal arithmetics library for C.

[![Build Status](https://travis-ci.org/shdown/libdeci.svg?branch=master)](https://travis-ci.org/shdown/libdeci)

Features:

  * supports GNU GCC, Clang, ICC, MSVC;

  * does not require extra memory for any operation, thus never allocates memory;

  * compiles as both C and C++;

  * hardware-independent ([it even works on WebAssembly](https://shdown.github.io/deci/demo.html));

  * simple;

  * fast;

  * well-tested.

# Simplicity, performance and algorithms

In libdeci, only the “basecase” (quadratic) algorithms are implemented for multiplication, division
and conversion to/from binary. There are multiple reasons for that:

  * In the world of arbitrary-precision arithmetic especially, fancy algorithms are slow when N is
small, thus any reasonable implementation that employs them tends to fall back to dumb quadratic
algorithms if N is less than some threshold. It is for this reason, combined with the fact that
many of the fancy algorithms are divide-and-conquer in nature, the dumb algorithms are also
called “basecase” algorithms. So even if you need to use a fancy algorithm to crunch very large
decimal numbers, you will still need the “basecase” algorithms.

  * If you are crunching very large *decimal* numbers with fancy algorithms, most likely you are
doing something wrong: binary arithmetics is native for computers, so consider using
[GMP](https://gmplib.org/) in the first place; or, alternatively, convert “to bits” before doing
costly computation, and convert the result back afterwards.

  * Fancy algorithms introduce complexity.

  * Fancy algorithms allocate extra memory.

## But still, what’s your plan?

  * For multiplication, we have:

    - [libdeci-kara](https://github.com/shdown/libdeci-kara) that implements intermediate-fanciness multiplication via Karatsuba algorithm;

    - [libdeci-ntt](https://github.com/shdown/libdeci-ntt) that implements high-fanciness multiplication via the number-theoretic transform (variant of Fourier transform).

  * [Divide-and-conquer algorithm](https://gmplib.org/manual/Divide-and-Conquer-Division#Divide-and-Conquer-Division) for division.

  * [Divide-and-conquer algorithms](http://www.numberworld.org/y-cruncher/internals/radix-conversion.html)
for decimal-to-binary and binary-to-decimal radix conversion.
Note that in order to use those to convert *to* binary, you need to be able to multiply big binary
numbers; and in order to convert *from* binary, you need to be able to divide big binary numbers.
We recommend using [GMP](https://gmplib.org/) for that (more specifically, the
[low-level functions](https://gmplib.org/manual/Low_002dlevel-Functions) operating directly on
`mp_limb_t` spans).
