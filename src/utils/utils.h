/**
 * This file is a part of ThetaRush.
 *
 * ThetaRush is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ThetaRush is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * some are taken from https://github.com/BetaFlight/betaflight/
 */

#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DUMMY                                                                  \
  do {                                                                         \
  } while (0)

#define NOP                         DUMMY

#define ARRAYLEN(x)                 (sizeof(x) / sizeof(x)[0])
#define ARRAYEND(x)                 (&(x)[ARRAYLEN(x)])

#define FORCED_TYPE_CONV(type, var) ((type)(var))

#define __TOSTRING(x)               #x
#define TOSTRING(x)                 __TOSTRING(x)

#define __CONTACT(x, y)             x##y
#define CONTACT(x, y)               __CONTACT(x, y)
#define CONTACT2(_1, _2)            CONTACT(_1, _2)
#define CONTACT3(_1, _2, _3)        CONTACT(CONTACT(_1, _2), _3)
#define CONTACT4(_1, _2, _3, _4)    CONTACT(CONTACT3(_1, _2, _3), _4)

#if !defined(UNUSED)
#define UNUSED(x) ((void)(x))
#endif /* UNUSE */

#if !defined(MAYBE_UNUSE)
#define MAYBE_UNUSE(x) UNUSED(x)
#endif /* MAYBE_UNUSED */

#if !defined(DEFAULT_NONE_INTERFACE)
#define DEFAULT_NONE_INTERFACE NULL
#endif /* DISCARD_INTERFACE */

#if !defined(WEAKFN)
#define WEAKFN __attribute__((weak))
#endif /* WEAKFN */

#define BIT(x)        (1 << (x))

#define COMPVAL(x, y) ((x) - (y))

#define MAX(a, b)     ((a) > (b) ? (a) : (b))
#define MIN(a, b)     ((a) < (b) ? (a) : (b))

#define FLOATEQU(f, v)                                                         \
  (((double)(f) - (double)(v)) < 1e-15 && ((double)(f) - (double)(v)) > -1e-15 \
       ? 1                                                                     \
       : 0)
#define FLOATNBR(f, v)                                                         \
  (((double)(f) - (double)(v)) < 1e-10 && ((double)(f) - (double)(v)) > -1e-10 \
       ? 1                                                                     \
       : 0)

#define SWAPI(x, y)                                                            \
  do {                                                                         \
    x = x + y;                                                                 \
    y = x - y;                                                                 \
    x = x - y;                                                                 \
  } while (0)

#define SWAPF SWAPI

static inline int16_t compUI16(uint16_t a, uint16_t b) {
  return (int16_t)(a - b);
}

static inline int32_t compUI32(uint32_t a, uint32_t b) {
  return (int32_t)(a - b);
}

static inline int32_t swapI32(int32_t x) {
  return (
      (((uint32_t)x & 0x000000ffU) << 24) | (((uint32_t)x & 0x0000ff00U) << 8) |
      (((uint32_t)x & 0x00ff0000U) >> 8) | (((uint32_t)x & 0xff000000U) >> 24));
}

static inline int16_t swapI16(int16_t x) {
  return ((((uint16_t)x & 0x00ffU) << 8) | (((uint16_t)x & 0xff00U) >> 8));
}

/*
 * https://groups.google.com/forum/?hl=en#!msg/comp.lang.c/attFnqwhvGk/sGBKXvIkY3AJ
 * Return (v ? floor(log2(v)) : 0) when 0 <= v < 1<<[8, 16, 32, 64].
 * Inefficient algorithm, intended for compile-time constants.
 */
#define LOG2_8BIT(v)  (8 - 90 / (((v) / 4 + 14) | 1) - 2 / ((v) / 2 + 1))
#define LOG2_16BIT(v) (8 * ((v) > 255) + LOG2_8BIT((v) >> 8 * ((v) > 255)))
#define LOG2_32BIT(v)                                                          \
  (16 * ((v) > 65535L) + LOG2_16BIT((v) * 1L >> 16 * ((v) > 65535L)))
#define LOG2_64BIT(v)                                                          \
  (32 * ((v) / 2L >> 31 > 0) +                                                 \
   LOG2_32BIT((v) * 1L >> 16 * ((v) / 2L >> 31 > 0) >>                         \
              16 * ((v) / 2L >> 31 > 0)))
#define LOG2(v) LOG2_64BIT(v)

// non ISO variant from linux kernel; checks ptr type, but triggers 'ISO C
// forbids braced-groups within expressions [-Wpedantic]'
//  __extension__ is here to disable this warning
#define container_of(ptr, type, member)                                        \
  (__extension__({                                                             \
    const typeof(((type *)0)->member) *__mptr = (ptr);                         \
    (type *)((char *)__mptr - offsetof(type, member));                         \
  }))

#if __GNUC__ > 6
#define FALLTHROUGH                                                            \
  ;                                                                            \
  __attribute__((fallthrough))
#else
#define FALLTHROUGH                                                            \
  do {                                                                         \
  } while (0)
#endif

#ifdef __cplusplus
}
#endif