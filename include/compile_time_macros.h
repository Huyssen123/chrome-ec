/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Handy clever tricks */

#ifndef __CROS_EC_COMPILE_TIME_MACROS_H
#define __CROS_EC_COMPILE_TIME_MACROS_H

/* Test an important condition at compile time, not run time */
#define _BA1_(cond, line) \
	extern int __build_assertion_ ## line[1 - 2*!(cond)] \
	__attribute__ ((unused))
#define _BA0_(c, x) _BA1_(c, x)
#define BUILD_ASSERT(cond) _BA0_(cond, __LINE__)

/*
 * Test an important condition inside code path at run time, taking advantage of
 * -Werror=div-by-zero.
 */
#define BUILD_CHECK_INLINE(value, cond_true) ((value) / (!!(cond_true)))

/* Number of elements in an array */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* Make for loops that iterate over pointers to array entries more readable */
#define ARRAY_BEGIN(array) (array)
#define ARRAY_END(array) ((array) + ARRAY_SIZE(array))

/* Just in case - http://gcc.gnu.org/onlinedocs/gcc/Offsetof.html */
#ifndef offsetof
#define offsetof(type, member)  __builtin_offsetof(type, member)
#endif

#define member_size(type, member) sizeof(((type *)0)->member)

/*
 * Bit operation macros.
 */
#define BIT(nr)			(1UL << (nr))
#define BIT_ULL(nr)		(1ULL << (nr))

/*
 * Create a bit mask from least significant bit |l|
 * to bit |h|, inclusive.
 *
 * Examples:
 * GENMASK(31, 0) ==> 0xFF_FF_FF_FF
 * GENMASK(3, 0)  ==> 0x00_00_00_0F
 * GENMASK(7, 4)  ==> 0x00_00_00_F0
 * GENMASK(b, b)  ==> BIT(b)
 *
 * Note that we shift after using BIT() to avoid compiler
 * warnings for BIT(31+1).
 */
#define GENMASK(h, l)     (((BIT(h)<<1)     - 1) ^ (BIT(l)     - 1))
#define GENMASK_ULL(h, l) (((BIT_ULL(h)<<1) - 1) ^ (BIT_ULL(l) - 1))

#endif /* __CROS_EC_COMPILE_TIME_MACROS_H */
