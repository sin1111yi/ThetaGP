/**
 * This file is a part of ThetaGP.
 *
 * ThetaGP is free software: you can redistribute it
 * and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * ThetaGP is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "BoardConfig.h"

// Firmware-level defaults. These are properties of the firmware, not of a
// board: every target behaves the same unless a board or a build overrides a
// value. BoardConfig.h is included first so a board-level definition wins, and
// each macro below is wrapped in #ifndef so an override needs no edit here.

// ── Logging ──
// Logging is on with a Debug threshold in a debug build and compiled out of a
// release build, which is what the build type implies; NDEBUG is defined by
// the release flags. The level macro must name a LogLevel enumerator (Info,
// Warn, Error, Debug) because the logger pastes it into LOG_LV().
#if defined(NDEBUG)
#ifndef THETAGP_CFG_LOG_EN
#define THETAGP_CFG_LOG_EN 0
#endif
#ifndef THETAGP_CFG_LOG_LV
#define THETAGP_CFG_LOG_LV Error
#endif
#else
#ifndef THETAGP_CFG_LOG_EN
#define THETAGP_CFG_LOG_EN 1
#endif
#ifndef THETAGP_CFG_LOG_LV
#define THETAGP_CFG_LOG_LV Debug
#endif
#endif

// ── USB report path ──
// The link mode sets the ceiling. Full speed polls the interrupt endpoint once
// per 1 ms frame, so it carries at most 1000 reports/s; high speed polls once
// per 125 us microframe, so it reaches 8000. USBHW_SPEED_* comes from the
// board's [usb] speed setting.
//
// On high speed the rate is a choice rather than a given: the default keeps
// parity with full speed, and raising it to the microframe ceiling is worth it
// only when the host really polls that fast and the CPU budget is there.
#if defined(USBHW_SPEED_HS)
#define THETAGP_CFG_USB_REPORT_RATE_MAX_HZ 8000
#ifndef THETAGP_CFG_USB_REPORT_RATE_HZ
#define THETAGP_CFG_USB_REPORT_RATE_HZ 1000
#endif
#elif defined(USBHW_SPEED_FS)
#define THETAGP_CFG_USB_REPORT_RATE_MAX_HZ 1000
#ifndef THETAGP_CFG_USB_REPORT_RATE_HZ
#define THETAGP_CFG_USB_REPORT_RATE_HZ 1000
#endif
#else
#error "[usb] speed not configured — set high_speed or full_speed in BoardConfig.toml"
#endif

// Asking for more than the link can carry would build a firmware that silently
// drops most of its reports, so it is rejected at configure time instead.
#if THETAGP_CFG_USB_REPORT_RATE_HZ > THETAGP_CFG_USB_REPORT_RATE_MAX_HZ
#error "THETAGP_CFG_USB_REPORT_RATE_HZ exceeds what the configured USB speed can carry"
#endif
#if THETAGP_CFG_USB_REPORT_RATE_HZ == 0
#error "THETAGP_CFG_USB_REPORT_RATE_HZ must be positive"
#endif

// ── Keypad scan path ──
// Matrix scan rate. Drive lines are walked one per interrupt, so a single key
// is re-read at this rate divided by the number of drive lines.
#ifndef THETAGP_CFG_KEYPAD_SCAN_HZ
#define THETAGP_CFG_KEYPAD_SCAN_HZ 32000
#endif

// Level-confirmation window. A raw level that contradicts the committed state
// must hold this long before the change is committed.
#ifndef THETAGP_CFG_KEYPAD_DEBOUNCE_PRESS_US
#define THETAGP_CFG_KEYPAD_DEBOUNCE_PRESS_US 125
#endif

// Lock window, started at every commit. Level flips inside it are swallowed
// rather than becoming a second commit, which is what absorbs contact bounce.
#ifndef THETAGP_CFG_KEYPAD_DEBOUNCE_HOLD_US
#define THETAGP_CFG_KEYPAD_DEBOUNCE_HOLD_US 3000
#endif

// Settle wait after a drive line changes level, before its columns are
// sampled. The default assumes a 30-50 kOhm pull-up against 10-20 pF of trace
// and pin capacitance; a board that changes either must recompute it.
#ifndef THETAGP_CFG_KEYPAD_GPIO_SETTLE_US
#define THETAGP_CFG_KEYPAD_GPIO_SETTLE_US 1
#endif
