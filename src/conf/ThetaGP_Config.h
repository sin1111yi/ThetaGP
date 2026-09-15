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

// ── Config-layer defaults ──
// The compiled-in values of the configuration fields, one macro per scalar
// field. Each is an #ifndef so a board or a build can change a single one with
// a bare -D. Values that only a board can know are not here: the key table is
// [keypad] button_map and stays in BoardConfig.h.

// SOCD cleaner mode (0..4).
#ifndef THETAGP_CFG_DEFAULT_SOCD_MODE
#define THETAGP_CFG_DEFAULT_SOCD_MODE 4                // Bypass — opposing directions both pass through
#endif

// Four-way gate for the dpad.
#ifndef THETAGP_CFG_DEFAULT_FOUR_WAY_MODE
#define THETAGP_CFG_DEFAULT_FOUR_WAY_MODE 0            // off — no direction is filtered out
#endif

// D-pad output mode.
#ifndef THETAGP_CFG_DEFAULT_DPAD_MODE
#define THETAGP_CFG_DEFAULT_DPAD_MODE 0                // 0 — native d-pad output
#endif

// Axis inversion and stick swap. 0 leaves the axis as it was read.
#ifndef THETAGP_CFG_DEFAULT_INV_X
#define THETAGP_CFG_DEFAULT_INV_X 0                    // off — left X not inverted
#endif
#ifndef THETAGP_CFG_DEFAULT_INV_Y
#define THETAGP_CFG_DEFAULT_INV_Y 0                    // off — left Y not inverted
#endif
#ifndef THETAGP_CFG_DEFAULT_INV_RX
#define THETAGP_CFG_DEFAULT_INV_RX 0                   // off — right X not inverted
#endif
#ifndef THETAGP_CFG_DEFAULT_INV_RY
#define THETAGP_CFG_DEFAULT_INV_RY 0                   // off — right Y not inverted
#endif
#ifndef THETAGP_CFG_DEFAULT_SWAP_STICKS
#define THETAGP_CFG_DEFAULT_SWAP_STICKS 0              // off — left and right sticks are not swapped
#endif

// Stick dead zones, in internal axis units: a 16-bit axis, 0..65535 with 32767
// as the centre (GAMEPAD_JOYSTICK_MIN / _MID / _MAX in gamepadstate.h).
#ifndef THETAGP_CFG_DEFAULT_LX_DZ
#define THETAGP_CFG_DEFAULT_LX_DZ 512                  // left X dead zone, in internal axis units
#endif
#ifndef THETAGP_CFG_DEFAULT_LY_DZ
#define THETAGP_CFG_DEFAULT_LY_DZ 512                  // left Y dead zone, in internal axis units
#endif
#ifndef THETAGP_CFG_DEFAULT_RX_DZ
#define THETAGP_CFG_DEFAULT_RX_DZ 512                  // right X dead zone, in internal axis units
#endif
#ifndef THETAGP_CFG_DEFAULT_RY_DZ
#define THETAGP_CFG_DEFAULT_RY_DZ 512                  // right Y dead zone, in internal axis units
#endif

// Stick sensitivity per axis; 128 is unity gain.
#ifndef THETAGP_CFG_DEFAULT_LX_SENS
#define THETAGP_CFG_DEFAULT_LX_SENS 128                // left X sensitivity, 128 = unity gain
#endif
#ifndef THETAGP_CFG_DEFAULT_LY_SENS
#define THETAGP_CFG_DEFAULT_LY_SENS 128                // left Y sensitivity, 128 = unity gain
#endif
#ifndef THETAGP_CFG_DEFAULT_RX_SENS
#define THETAGP_CFG_DEFAULT_RX_SENS 128                // right X sensitivity, 128 = unity gain
#endif
#ifndef THETAGP_CFG_DEFAULT_RY_SENS
#define THETAGP_CFG_DEFAULT_RY_SENS 128                // right Y sensitivity, 128 = unity gain
#endif

// Stick response shaping.
#ifndef THETAGP_CFG_DEFAULT_CURVE
#define THETAGP_CFG_DEFAULT_CURVE 0                    // no response curve applied
#endif
#ifndef THETAGP_CFG_DEFAULT_EMA
#define THETAGP_CFG_DEFAULT_EMA 0                      // no smoothing applied
#endif

// Trigger dead zones, in raw trigger units (0..255, rest 0).
#ifndef THETAGP_CFG_DEFAULT_LT_DZ
#define THETAGP_CFG_DEFAULT_LT_DZ 8                    // left trigger dead zone, in raw trigger units
#endif
#ifndef THETAGP_CFG_DEFAULT_RT_DZ
#define THETAGP_CFG_DEFAULT_RT_DZ 8                    // right trigger dead zone, in raw trigger units
#endif

// LED strip parameters. Brightness and animation speed are 0..255 with 128 as
// mid scale, saturation is 0..255 with 255 as full, hue is in degrees.
#ifndef THETAGP_CFG_DEFAULT_LED_BRIGHTNESS
#define THETAGP_CFG_DEFAULT_LED_BRIGHTNESS 128         // mid scale
#endif
#ifndef THETAGP_CFG_DEFAULT_LED_MODE
#define THETAGP_CFG_DEFAULT_LED_MODE 0                 // off — the strip is not lit by default
#endif
#ifndef THETAGP_CFG_DEFAULT_LED_HUE
#define THETAGP_CFG_DEFAULT_LED_HUE 180                // hue in degrees (0..359)
#endif
#ifndef THETAGP_CFG_DEFAULT_LED_SATURATION
#define THETAGP_CFG_DEFAULT_LED_SATURATION 255         // full saturation
#endif
#ifndef THETAGP_CFG_DEFAULT_LED_SPEED
#define THETAGP_CFG_DEFAULT_LED_SPEED 128              // mid scale
#endif

// Stick calibration offsets; 0 is "no offset", a freshly built device.
#ifndef THETAGP_CFG_DEFAULT_CAL_LX
#define THETAGP_CFG_DEFAULT_CAL_LX 0                   // no calibration offset
#endif
#ifndef THETAGP_CFG_DEFAULT_CAL_LY
#define THETAGP_CFG_DEFAULT_CAL_LY 0                   // no calibration offset
#endif
#ifndef THETAGP_CFG_DEFAULT_CAL_RX
#define THETAGP_CFG_DEFAULT_CAL_RX 0                   // no calibration offset
#endif
#ifndef THETAGP_CFG_DEFAULT_CAL_RY
#define THETAGP_CFG_DEFAULT_CAL_RY 0                   // no calibration offset
#endif
