/**
 * This file is a part of ThetaGP.
 *
 * ThetaGP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ThetaGP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "target/target_platform.h"

#include "BoardConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Abstract memory partition macros ──
// Composed from base section attributes in target/target_platform.h.
// New platforms: redefine DTCM_RAM_DATA / DTCM_RAM_BSS / RAM_DATA / RAM_BSS there.
//
// FAST_DATA            → fast CPU-local RAM (DTCM), initialized (Flash load image)
// FAST_DATA_ZERO_INIT  → fast CPU-local RAM (DTCM), zero-init (NOLOAD, no Flash copy)
// FAST_CODE            → function in DTCM RAM (.dtcmram_code, 0-wait, copied from Flash)
// FAST_CODE_PREF       → reserved — prefer fast RAM, fallback to Flash
// FAST_CODE_NOINLINE   → function in DTCM RAM, no inlining

#define FAST_DATA            DTCM_RAM_DATA
#define FAST_DATA_ZERO_INIT  DTCM_RAM_BSS
#define FAST_CODE            ITCM_RAM_CODE
#define FAST_CODE_PREF       /* reserved — code placement TBD */
#define FAST_CODE_NOINLINE   FAST_CODE __attribute__((noinline))

#define COMMON_DATA       RAM_DATA __attribute__((aligned(32)))
#define COMMON_ZERO_INIT  RAM_BSS __attribute__((aligned(32)))
#define COMMON_DATA_AUTO  static COMMON_DATA

#ifdef __cplusplus
}
#endif
