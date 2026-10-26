/**
 * This file is a part of hitbox-mcu.
 *
 * hitbox-mcu is free software: you can redistribute it
 * and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * hitbox-mcu is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// time difference, 32 bits always sufficient
typedef int32_t timeDelta_t;
// millisecond time
typedef uint32_t timeMs_t;
// microsecond time
#ifdef USE_64BIT_TIME
typedef uint64_t timeUs_t;
#define TIMEUS_MAX UINT64_MAX
#else
typedef uint32_t timeUs_t;
#define TIMEUS_MAX UINT32_MAX
#endif

typedef timeUs_t absolute_time_t;

static inline timeDelta_t cmpTimeUs(timeUs_t a, timeUs_t b) {
  return (timeDelta_t)(a - b);
}

static inline int32_t cmpTimeCycles(uint32_t a, uint32_t b) {
  return (int32_t)(a - b);
}

// The hardware timer's 32 bits of microseconds since power up.
uint32_t time_us_32(void);

// The microseconds since boot an absolute time stands for.
static inline uint32_t to_us_since_boot(absolute_time_t t) { return t; }

// Sets an absolute time to a number of microseconds since boot.
static inline void update_us_since_boot(absolute_time_t *t,
                                        uint32_t us_since_boot) {
  *t = us_since_boot;
}

// The absolute time of a number of microseconds since boot.
static inline absolute_time_t from_us_since_boot(uint32_t us_since_boot) {
  absolute_time_t t;
  update_us_since_boot(&t, us_since_boot);
  return t;
}

// The current time.
static inline absolute_time_t get_absolute_time(void) {
  absolute_time_t t;
  update_us_since_boot(&t, time_us_32());
  return t;
}

// Microseconds as milliseconds.
static inline uint32_t us_to_ms(uint64_t us) {
  if (us >> 32u) {
    return (uint32_t)(us / 1000u);
  } else {
    return ((uint32_t)us) / 1000u;
  }
}

// The milliseconds since boot an absolute time stands for.
static inline uint32_t to_ms_since_boot(absolute_time_t t) {
  uint64_t us = to_us_since_boot(t);
  return us_to_ms(us);
}

#ifdef __cplusplus
}
#endif
