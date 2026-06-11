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

#include <ArduinoJson.h>
#include <cstdint>

namespace ThetaGP::Gamepad::Config {

struct ConfigStore {
  // ── Map settings ──
  uint16_t btn_map[32];
  uint8_t  socd_mode = 0;
  uint8_t  four_way_mode = 0;
  uint8_t  dpad_mode = 0;
  uint8_t  inv_x = 0;
  uint8_t  inv_y = 0;
  uint8_t  inv_rx = 0;
  uint8_t  inv_ry = 0;
  uint8_t  swap_sticks = 0;

  // ── Stick settings ──
  uint16_t lx_dz = 512;
  uint16_t ly_dz = 512;
  uint16_t rx_dz = 512;
  uint16_t ry_dz = 512;
  uint8_t  lx_sens = 128;
  uint8_t  ly_sens = 128;
  uint8_t  rx_sens = 128;
  uint8_t  ry_sens = 128;
  uint8_t  curve = 0;
  uint8_t  ema = 0;

  // ── Trigger settings ──
  uint8_t  lt_dz = 8;
  uint8_t  rt_dz = 8;

  // ── USB settings ──
  uint8_t  poll_rate = 2;

  // ── LED settings ──
  uint8_t  led_brightness = 128;
  uint8_t  led_mode = 0;
  uint16_t led_hue = 180;
  uint8_t  led_saturation = 255;
  uint8_t  led_speed = 128;

  // ── System settings ──
  uint8_t  log_level = 1;
  uint8_t  debounce_samples = 16;
  uint8_t  debounce_threshold = 12;

  // ── Calibration ──
  int16_t  cal_lx = 0;
  int16_t  cal_ly = 0;
  int16_t  cal_rx = 0;
  int16_t  cal_ry = 0;
};

/** Parse JSON profile body into ConfigStore. */
void parseProfile(const char *json, ConfigStore *cfg);

/** Runtime working copy of the active configuration. */
COMMON_ZERO_INIT extern ConfigStore s_config;

} // namespace ThetaGP::Gamepad::Config
