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

/*! \brief Return the current 32 bit timestamp value in microseconds for the
 * default timer instance
 *  \ingroup hardware_timer
 *
 * Returns the full 32 bits of the hardware timer. The \ref pico_time and other
 * functions rely on the fact that this value monotonically increases from power
 * up. As such it is expected that this value counts upwards and never wraps (we
 * apologize for introducing a potential year 5851444 bug).
 *
 * \return the 32 bit timestamp
 * \sa timer_time_us_32
 * */
uint32_t time_us_32(void);

/*! fn to_us_since_boot
 * \brief convert an absolute_time_t into a number of microseconds since boot.
 * \param t the absolute time to convert
 * \return a number of microseconds since boot, equivalent to t
 * \ingroup timestamp
 */
static inline uint32_t to_us_since_boot(absolute_time_t t) { return t; }

/*! fn update_us_since_boot
 * \brief update an absolute_time_t value to represent a given number of
 * microseconds since boot
 * \param t the absolute time value to update
 * \param us_since_boot the number of microseconds since boot to represent.
 * Note this should be representable as a signed 64 bit integer
 * \ingroup timestamp
 */
static inline void update_us_since_boot(absolute_time_t *t,
                                        uint32_t us_since_boot) {
  *t = us_since_boot;
}

/*! fn from_us_since_boot
 * \brief convert a number of microseconds since boot to an absolute_time_t
 * \param us_since_boot number of microseconds since boot
 * \return an absolute time equivalent to us_since_boot
 * \ingroup timestamp
 */
static inline absolute_time_t from_us_since_boot(uint32_t us_since_boot) {
  absolute_time_t t;
  update_us_since_boot(&t, us_since_boot);
  return t;
}

/*! \brief Return a representation of the current time.
 * \ingroup timestamp
 *
 * Returns an opaque high fidelity representation of the current time sampled
 * during the call.
 *
 * \return the absolute time (now) of the hardware timer
 *
 * \sa absolute_time_t
 * \sa sleep_until()
 * \sa time_us_32()
 */
static inline absolute_time_t get_absolute_time(void) {
  absolute_time_t t;
  update_us_since_boot(&t, time_us_32());
  return t;
}

static inline uint32_t us_to_ms(uint64_t us) {
  if (us >> 32u) {
    return (uint32_t)(us / 1000u);
  } else {
    return ((uint32_t)us) / 1000u;
  }
}

/*! fn to_ms_since_boot
 * \ingroup timestamp
 * \brief Convert a timestamp into a number of milliseconds since boot.
 * \param t an absolute_time_t value to convert
 * \return the number of milliseconds since boot represented by t
 * \sa to_us_since_boot()
 */
static inline uint32_t to_ms_since_boot(absolute_time_t t) {
  uint64_t us = to_us_since_boot(t);
  return us_to_ms(us);
}

#ifdef __cplusplus
}
#endif
