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
 * You should have received a copy of the GNU General Public
 * License along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "build_info.h"

#include <cstdint>

namespace ThetaGP::Gamepad::Config {

// No field carries an initializer. The defaults are aggregated in one object in
// config_defaults.h (kConfigDefaults); this struct has none of its own. A
// ConfigStore nothing has assigned to it is zero, not a usable config.
struct ConfigStore {
  // ── Map settings ──
  uint8_t btn_map[32]; // Physical key → button bit index (0xFF = unmapped)
  uint8_t socd_mode;
  uint8_t four_way_mode;
  uint8_t dpad_mode;
  uint8_t inv_x;
  uint8_t inv_y;
  uint8_t inv_rx;
  uint8_t inv_ry;
  uint8_t swap_sticks;

  // ── Stick settings ──
  uint16_t lx_dz;
  uint16_t ly_dz;
  uint16_t rx_dz;
  uint16_t ry_dz;
  uint8_t lx_sens;
  uint8_t ly_sens;
  uint8_t rx_sens;
  uint8_t ry_sens;
  uint8_t curve;
  uint8_t ema;

  // ── Trigger settings ──
  uint8_t lt_dz;
  uint8_t rt_dz;

  // ── LED settings ──
  uint8_t led_brightness;
  uint8_t led_mode;
  uint16_t led_hue;
  uint8_t led_saturation;
  uint8_t led_speed;

  // ── Calibration ──
  int16_t cal_lx;
  int16_t cal_ly;
  int16_t cal_rx;
  int16_t cal_ry;
};

// Parse the JSON profile body `json[0 .. len)` into ConfigStore. `len` is the
// body's length in bytes, and it is what bounds the parse: the body needs no
// terminator, and no byte behind it is read even when it fills the buffer it
// sits in whole. Pass the length the store reported for the body being parsed.
void parseProfile(const char *json, uint32_t len, ConfigStore *cfg);

// Write cfg into dst as a profile JSON body. `cap` is the size of dst in bytes,
// terminator included. Returns the body length in bytes, terminator not
// counted, or 0 when no usable body was produced: dst was too small for it, or
// the body would be longer than PROFILE_JSON_MAX, the most the flash layer
// stores. A truncated body is never returned — 0 means "nothing to persist", so
// a caller must not hand that length to the store.
uint16_t serializeProfile(const ConfigStore &cfg, char *dst, uint16_t cap);

} // namespace ThetaGP::Gamepad::Config
