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

// Firmware-level defaults: the behaviour the firmware has whichever board it is
// built for. BoardConfig.h is included first and every macro below is wrapped in
// #ifndef, so a board definition wins. Each knob carries its unit on the line
// above it.

// ── Task counters ──
// Whether sys.get_task_info carries run_count and late_count. The scheduler
// keeps both in every build; this gates their reporting.
#ifndef THETAGP_CFG_TASK_COUNTERS
#define THETAGP_CFG_TASK_COUNTERS 0
#endif

// ── PS4 authentication key ──
// Whether the build carries the self-signed key the authentication channel is
// exercised with: the flow is the real one and no console accepts what it
// signs. The key is src/drivers/gp_emulator/ps4/ps4_test_key.h.
#ifndef THETAGP_CFG_PS4_TEST_KEY
#define THETAGP_CFG_PS4_TEST_KEY 0
#endif

// ── TinyUSB debug ──
// Log level of the TinyUSB stack. The build hands it to the library as
// CFG_TUSB_DEBUG.
#ifndef THETAGP_CFG_USB_DBG
#define THETAGP_CFG_USB_DBG 0
#endif

// ── Car simulator ──
// Whether the generic HID device is the car simulator instead of the gamepad:
// the board's analog channels as five axes, and its shifter as the gear.
#ifndef THETAGP_CFG_CARSIM
#define THETAGP_CFG_CARSIM 0
#endif

// ── Logging ──
// Logging is on in a debug build and compiled out of a release one. The level is
// the threshold the firmware writes at until a profile is read; the profile
// carries its own as `sys.log`, one of the three levels it chooses among.
//
// Debug lines are a build switch rather than a threshold.
#if defined(NDEBUG)
#ifndef THETAGP_CFG_LOG_EN
#define THETAGP_CFG_LOG_EN 0
#endif
#ifndef THETAGP_CFG_LOG_LV
#define THETAGP_CFG_LOG_LV Error
#endif
#ifndef THETAGP_CFG_LOG_DEBUG
#define THETAGP_CFG_LOG_DEBUG 0
#endif
#else
#ifndef THETAGP_CFG_LOG_EN
#define THETAGP_CFG_LOG_EN 1
#endif
#ifndef THETAGP_CFG_LOG_LV
#define THETAGP_CFG_LOG_LV Info
#endif
#ifndef THETAGP_CFG_LOG_DEBUG
#define THETAGP_CFG_LOG_DEBUG 1
#endif
#endif

// ── Key toggle test ──
// Test hook: the keypad read returns a mask with one bit flipped per call, so
// every report differs from the last one. Compiled out when disabled.
#ifndef THETAGP_CFG_KEY_TOGGLE_EN
#define THETAGP_CFG_KEY_TOGGLE_EN 0
#endif

// ── Command path ──
// The rate the protocol's command task ticks at: the wait a caller pays per frame
// on top of the wire's own. Its window spans a report period whenever it is longer
// than one, so a faster report rate wants a slower command tick.
#ifndef THETAGP_CFG_CMD_PROC_HZ
#define THETAGP_CFG_CMD_PROC_HZ 100
#endif

// ── USB report path ──
// The link sets the ceiling: 1000 reports/s at full speed, one poll per 1 ms
// frame, and 8000 at high speed, one per 125 us microframe.
#if defined(BDCFG_SPEED_HS)
#define THETAGP_CFG_USB_REPORT_RATE_MAX_HZ 8000
#elif defined(BDCFG_SPEED_FS)
#define THETAGP_CFG_USB_REPORT_RATE_MAX_HZ 1000
#else
#error "[usb] speed not configured — set high_speed or full_speed in BoardConfig.toml"
#endif

// Rate the gamepad task ticks at. A board declares its own in [usb]
// wired_report_hz, which reaches here as BDCFG_REPORT_RATE_HZ. A report goes
// out only when the state changes, so the rate is a ceiling.
#ifndef THETAGP_CFG_USB_REPORT_RATE_HZ
#ifdef BDCFG_REPORT_RATE_HZ
#define THETAGP_CFG_USB_REPORT_RATE_HZ BDCFG_REPORT_RATE_HZ
#else
#define THETAGP_CFG_USB_REPORT_RATE_HZ 1000
#endif
#endif

// A rate the link cannot carry would drop most of its reports, so it is refused
// at configure time.
#if THETAGP_CFG_USB_REPORT_RATE_HZ > THETAGP_CFG_USB_REPORT_RATE_MAX_HZ
#error "THETAGP_CFG_USB_REPORT_RATE_HZ exceeds what the configured USB speed can carry"
#endif
#if THETAGP_CFG_USB_REPORT_RATE_HZ <= 0
#error "THETAGP_CFG_USB_REPORT_RATE_HZ must be positive"
#endif
// The task period is a whole number of microseconds, so the rate has to divide
// 1000000.
#if THETAGP_CFG_USB_REPORT_RATE_HZ > 0 && 1000000 % THETAGP_CFG_USB_REPORT_RATE_HZ != 0
#error "THETAGP_CFG_USB_REPORT_RATE_HZ must divide 1000000 so the task period is a whole number of microseconds"
#endif

// ── Flash presence ──
// Whether the board carries a flash chip, as BDCFG_HAS_FLASH states it. A board
// config that states nothing leaves the macro undefined, which reads as absent.
#ifndef THETAGP_CFG_HAS_FLASH
#ifdef BDCFG_HAS_FLASH
#define THETAGP_CFG_HAS_FLASH BDCFG_HAS_FLASH
#endif
#endif

// ── RGB strip presence ──
// Whether the board carries a strip, as BDCFG_HAS_RGB_STRIP states it.
#ifndef THETAGP_CFG_HAS_RGB_STRIP
#ifdef BDCFG_HAS_RGB_STRIP
#define THETAGP_CFG_HAS_RGB_STRIP BDCFG_HAS_RGB_STRIP
#endif
#endif

// ── Keypad scan path ──
// Matrix scan rate: one callback walks every drive line, so a key is sampled at
// this rate. The filter's windows live in keypad.h.
#ifndef THETAGP_CFG_KEYPAD_SCAN_HZ
#define THETAGP_CFG_KEYPAD_SCAN_HZ 32000
#endif

// A zero rate would leave the scan timer with no period to run at.
#if THETAGP_CFG_KEYPAD_SCAN_HZ == 0
#error "THETAGP_CFG_KEYPAD_SCAN_HZ must be positive"
#endif

// ── LED effect path ──
// The strip's rate is the config key `led.rgb.hz`, in frames a second; the task
// period below is how often the refresh runs, and one frame covers at least one
// tick.
#ifndef THETAGP_CFG_LED_TASK_PERIOD_US
#define THETAGP_CFG_LED_TASK_PERIOD_US 10000UL         // 100 Hz
#endif

// The share of full-scale colour the strip is driven at: 1.0 leaves a channel
// as it is, 0.25 caps it at a quarter.
#ifndef THETAGP_CFG_LED_BRIGHTNESS_LIMIT
#define THETAGP_CFG_LED_BRIGHTNESS_LIMIT 0.25f
#endif

// The chips a frame drives past the strip's own, each a black pixel: a chip the
// frame never reaches would hold what it latched, and the last one needs clocks
// after its own data to latch on.
#ifndef THETAGP_CFG_LED_TRAILING_PIXELS
#define THETAGP_CFG_LED_TRAILING_PIXELS 4
#endif
