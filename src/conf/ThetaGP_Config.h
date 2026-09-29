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

// Firmware-level defaults: this file is where the software behaviour of the
// firmware is configured. BoardConfig.h is included first and every macro below
// is wrapped in #ifndef, so a board definition wins. The build forwards no
// THETAGP_CFG_ switch (see src/CMakeLists.txt), so a value below is changed by
// editing it here.

// ── Index ──
// Every knob in this file with its default. The per-knob block below carries
// the unit and the meaning; this list is only so a reader sees the whole set
// in one screen without scrolling.
//
//   build
//     THETAGP_CFG_USB_DBG                   0     TinyUSB verbose debug (the build
//                                                  forwards it as CFG_TUSB_DEBUG)
//     THETAGP_CFG_LOG_EN / THETAGP_CFG_LOG_LV     derived from the build type
//   protocol
//     THETAGP_CFG_TASK_COUNTERS             0     report the two per-task counters
//                                                  in sys.get_task_info
//   report path
//     THETAGP_CFG_USB_REPORT_RATE_HZ        1000  gamepad task rate, <= link ceiling
//     THETAGP_CFG_USB_REPORT_RATE_MAX_HZ          derived from the board's USB speed
//     THETAGP_CFG_KEY_TOGGLE_EN             0     test hook, flips one key bit per read
//   keypad scan path
//     THETAGP_CFG_KEYPAD_SCAN_HZ            32000
//   led effect path
//     THETAGP_CFG_LED_TASK_PERIOD_US        10000   refresh task period
//     THETAGP_CFG_LED_EFFECT_PERIOD_US      1000000 one animation cycle
//   config-layer defaults (see the blocks for units and meanings)
//     SOCD_MODE 4      FOUR_WAY_MODE 0   DPAD_MODE 0
//     INV_X/Y/RX/RY 0  SWAP_STICKS 0
//     LX/LY/RX/RY_DZ 512                LX/LY/RX/RY_SENS 128
//     CURVE 0          EMA 0            LT_DZ 8     RT_DZ 8
//     LED_BRIGHTNESS 128  LED_MODE 0    LED_HUE 180 LED_SATURATION 255  LED_SPEED 128
//     CAL_LX/LY/RX/RY 0

// ── Task counters ──
// Whether sys.get_task_info reports the scheduler's two per-task counters,
// run_count and late_count. Both are optional fields of that reply, so a build
// that leaves the switch off answers without them and a host reads their
// absence as a device that does not report counters. The scheduler maintains
// the counters in every build: this gates only the copy and the reporting.
#ifndef THETAGP_CFG_TASK_COUNTERS
#define THETAGP_CFG_TASK_COUNTERS 0
#endif

// ── TinyUSB debug ──
// Log level of the TinyUSB stack itself. The value lives here with the rest of
// the configuration, but the build has to hand it to the library as
// CFG_TUSB_DEBUG: a definition in this header only reaches the firmware's own
// translation units, not the library's.
#ifndef THETAGP_CFG_USB_DBG
#define THETAGP_CFG_USB_DBG 0
#endif

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

// ── Key toggle test ──
// Test hook for exercising the report path. When enabled the keypad read
// returns a mask with one bit flipped per call, so every report differs from
// the last one submitted. The HID driver sends only when the report differs
// from the last one it handed to the stack, so without such a toggle an idle
// pad produces no traffic. Compiled out entirely when disabled.
#ifndef THETAGP_CFG_KEY_TOGGLE_EN
#define THETAGP_CFG_KEY_TOGGLE_EN 0
#endif

// ── USB report path ──
// The link mode sets the ceiling. Full speed polls the interrupt endpoint once
// per 1 ms frame, so it carries at most 1000 reports/s; high speed polls once
// per 125 us microframe, so it reaches 8000. BDCFG_SPEED_* comes from the
// board's [usb] speed setting.
#if defined(BDCFG_SPEED_HS)
#define THETAGP_CFG_USB_REPORT_RATE_MAX_HZ 8000
#elif defined(BDCFG_SPEED_FS)
#define THETAGP_CFG_USB_REPORT_RATE_MAX_HZ 1000
#else
#error "[usb] speed not configured — set high_speed or full_speed in BoardConfig.toml"
#endif

// Rate the gamepad task ticks at. A board that asks for its own rate declares
// it in [usb] wired_report_hz, which reaches here as BDCFG_REPORT_RATE_HZ;
// without it the default keeps parity with full speed. The rate is a ceiling
// rather than a constant packet stream: on high speed, raising it to the
// microframe ceiling is worth it only when the host really polls that fast and
// the CPU budget is there.
#ifndef THETAGP_CFG_USB_REPORT_RATE_HZ
#ifdef BDCFG_REPORT_RATE_HZ
#define THETAGP_CFG_USB_REPORT_RATE_HZ BDCFG_REPORT_RATE_HZ
#else
#define THETAGP_CFG_USB_REPORT_RATE_HZ 1000
#endif
#endif

// Asking for more than the link can carry would build a firmware that silently
// drops most of its reports, so it is rejected at configure time instead.
#if THETAGP_CFG_USB_REPORT_RATE_HZ > THETAGP_CFG_USB_REPORT_RATE_MAX_HZ
#error "THETAGP_CFG_USB_REPORT_RATE_HZ exceeds what the configured USB speed can carry"
#endif
#if THETAGP_CFG_USB_REPORT_RATE_HZ <= 0
#error "THETAGP_CFG_USB_REPORT_RATE_HZ must be positive"
#endif
// The task period is a whole number of microseconds (TASK_PERIOD_HZ divides
// 1000000 by the rate), so a rate that does not divide 1000000 cannot be kept:
// the period is truncated and the tick lands on a rate nothing asked for. The
// guard keeps the modulo off a zero rate, which the check above already rejects.
#if THETAGP_CFG_USB_REPORT_RATE_HZ > 0 && 1000000 % THETAGP_CFG_USB_REPORT_RATE_HZ != 0
#error "THETAGP_CFG_USB_REPORT_RATE_HZ must divide 1000000 so the task period is a whole number of microseconds"
#endif

// ── Flash presence ──
// Whether the board carries a flash chip, under the name the firmware reads.
// The board config always states it, as BDCFG_HAS_FLASH: 0 for a board that
// declares chip = "none", 1 for a board that declares a chip. The outer
// #ifndef leaves a build-time definition (a bare -D) in charge of the value.
//
// There is no #else. The bridge carries the board's answer and does not invent
// one: a board config that states nothing leaves THETAGP_CFG_HAS_FLASH
// undefined, which an #if reads as 0 and an #ifdef reads as absent, instead of
// defined as 0, which an #ifdef would read as present.
#ifndef THETAGP_CFG_HAS_FLASH
#ifdef BDCFG_HAS_FLASH
#define THETAGP_CFG_HAS_FLASH BDCFG_HAS_FLASH
#endif
#endif

// ── Keypad scan path ──
// Matrix scan rate. One scan callback walks every drive line, so a single key
// is sampled at this rate. The filter commits a level change from the same
// samples, one decision per scan; its windows live in keypad.h.
#ifndef THETAGP_CFG_KEYPAD_SCAN_HZ
#define THETAGP_CFG_KEYPAD_SCAN_HZ 32000
#endif

// A zero rate would leave the scan timer with no period to run at.
#if THETAGP_CFG_KEYPAD_SCAN_HZ == 0
#error "THETAGP_CFG_KEYPAD_SCAN_HZ must be positive"
#endif

// ── LED effect path ──
// The strip's animation runs off two periods. The task period is how often the
// refresh task runs; the effect period is how long one full cycle of the
// animation takes, which the frame interval comes out of as periodUs /
// FRAME_COUNT. The frame interval has to cover at least one task period, or the
// task cannot show every frame it advances through (led_effect_task.cpp asserts
// it at compile time).
#ifndef THETAGP_CFG_LED_TASK_PERIOD_US
#define THETAGP_CFG_LED_TASK_PERIOD_US 10000UL         // 100 Hz
#endif
#ifndef THETAGP_CFG_LED_EFFECT_PERIOD_US
#define THETAGP_CFG_LED_EFFECT_PERIOD_US 1000000UL     // 1 s per cycle, 20 ms per frame
#endif
