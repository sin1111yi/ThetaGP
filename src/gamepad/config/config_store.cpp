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

#include "gamepad/config/config_store.h"
#include "utils/log/log.h"

// PROFILE_JSON_MAX: the body length the flash layer accepts. The serializer has
// to refuse anything longer here rather than at the write, so that a body can
// never be truncated into a well-formed-looking prefix.
#include "gamepad/profile/profile_store.h"

// Compiles the defaults into this translation unit, which is what runs its
// static_asserts: the defaults header holds the compile-time checks on the
// board key table, and a header no translation unit includes is never checked.
#include "gamepad/config/config_defaults.h"
#include "gamepad/gamepadenums.h"
#include "utils/json/json.h"

#include <limits>

namespace ThetaGP::Gamepad::Config {

// An unmapped btn_map slot is 0xFF on the wire, and kBtnMapUnmapped is the byte
// config_defaults.h gives a key the board leaves out. The two spellings live in
// different files, so this holds them together.
static_assert(detail::kBtnMapUnmapped == 0xFF,
              "kBtnMapUnmapped: the unmapped btn_map slot is 0xFF on the wire");

// ── Field domains ──
// A scalar a profile carries is stored only when the number lies inside the
// field's domain; a number outside it leaves the field at its compiled default
// from kConfigDefaults, the one place the defaults live. Narrowing the number
// instead would store a value no profile carried: 300 into a uint8_t field
// stores 44, a legal-looking number with a different meaning. A key the profile
// does not carry at all leaves the field as it stands, which is the default
// getInt() is handed.

// The values the field's type holds.
template <typename T> static T fieldValue(int value, T fallback) {
  if (value < static_cast<int>(std::numeric_limits<T>::min()) ||
      value > static_cast<int>(std::numeric_limits<T>::max())) {
    return fallback;
  }
  return static_cast<T>(value);
}

// A field whose meaning is a set of named values: the enumerators 0 up to, but
// not including, count.
static uint8_t enumValue(int value, uint8_t fallback, int count) {
  if (value < 0 || value >= count) {
    return fallback;
  }
  return static_cast<uint8_t>(value);
}

// A field that is on or off: the two values a bool holds, and no others.
static uint8_t flagValue(int value, uint8_t fallback) {
  if (value != static_cast<int>(false) && value != static_cast<int>(true)) {
    return fallback;
  }
  return static_cast<uint8_t>(value);
}

void parseProfile(const char *json, ConfigStore *cfg) {
  if (!json || !cfg) {
    LOG_ERROR("parseProfile: null args");
    return;
  }

  Json doc;
  doc.parse(json);

  // The value a field takes when the profile carries a number its domain does
  // not hold.
  const ConfigStore &def = kConfigDefaults;

  // ── map ──
  // socd_mode names a SOCDMode enumerator, so that enum bounds it. The fields
  // after it mean on or off, so a bool's two values bound them. dpad_mode has
  // no domain narrower than its byte: the firmware names no D-pad output modes
  // for it to be checked against.
  cfg->socd_mode =
      enumValue(doc.getInt("map.socd", cfg->socd_mode), def.socd_mode,
                static_cast<int>(Enums::SOCDMode::Count));
  cfg->four_way_mode = flagValue(doc.getInt("map.four_way", cfg->four_way_mode),
                                 def.four_way_mode);
  cfg->dpad_mode =
      fieldValue(doc.getInt("map.dpad", cfg->dpad_mode), def.dpad_mode);
  cfg->inv_x = flagValue(doc.getInt("map.inv_x", cfg->inv_x), def.inv_x);
  cfg->inv_y = flagValue(doc.getInt("map.inv_y", cfg->inv_y), def.inv_y);
  cfg->inv_rx = flagValue(doc.getInt("map.inv_rx", cfg->inv_rx), def.inv_rx);
  cfg->inv_ry = flagValue(doc.getInt("map.inv_ry", cfg->inv_ry), def.inv_ry);
  cfg->swap_sticks =
      flagValue(doc.getInt("map.swap", cfg->swap_sticks), def.swap_sticks);

  // btn_map array. A profile that carries no array leaves the table in place;
  // one that carries an array fills all 32 slots, and the slots it does not
  // reach take the sentinel.
  const int arrLen = doc.getArrLen("map.btn_map");
  if (arrLen > 0) {
    uint8_t idx = 0;
    for (int i = 0; i < arrLen && idx < 32; i++) {
      // An element is a button bit index or the unmapped sentinel. Any other
      // number would name a different button once narrowed to a byte; a
      // negative one, or one past the last bit, names no button at all.
      const int value = doc.getArrInt("map.btn_map", i, -1);
      cfg->btn_map[idx++] = (value >= 0 && value < detail::kBtnMaskBits)
                                ? static_cast<uint8_t>(value)
                                : detail::kBtnMapUnmapped;
    }
    while (idx < 32) {
      cfg->btn_map[idx++] = detail::kBtnMapUnmapped;
    }
  }

  // ── stick ──
  cfg->lx_dz = fieldValue(doc.getInt("stick.lx_dz", cfg->lx_dz), def.lx_dz);
  cfg->ly_dz = fieldValue(doc.getInt("stick.ly_dz", cfg->ly_dz), def.ly_dz);
  cfg->rx_dz = fieldValue(doc.getInt("stick.rx_dz", cfg->rx_dz), def.rx_dz);
  cfg->ry_dz = fieldValue(doc.getInt("stick.ry_dz", cfg->ry_dz), def.ry_dz);
  cfg->lx_sens =
      fieldValue(doc.getInt("stick.lx_sens", cfg->lx_sens), def.lx_sens);
  cfg->ly_sens =
      fieldValue(doc.getInt("stick.ly_sens", cfg->ly_sens), def.ly_sens);
  cfg->rx_sens =
      fieldValue(doc.getInt("stick.rx_sens", cfg->rx_sens), def.rx_sens);
  cfg->ry_sens =
      fieldValue(doc.getInt("stick.ry_sens", cfg->ry_sens), def.ry_sens);
  cfg->curve = fieldValue(doc.getInt("stick.curve", cfg->curve), def.curve);
  cfg->ema = fieldValue(doc.getInt("stick.ema", cfg->ema), def.ema);

  // ── trig ──
  cfg->lt_dz = fieldValue(doc.getInt("trig.lt_dz", cfg->lt_dz), def.lt_dz);
  cfg->rt_dz = fieldValue(doc.getInt("trig.rt_dz", cfg->rt_dz), def.rt_dz);

  // ── led ──
  cfg->led_brightness = fieldValue(doc.getInt("led.bri", cfg->led_brightness),
                                   def.led_brightness);
  cfg->led_mode =
      fieldValue(doc.getInt("led.mode", cfg->led_mode), def.led_mode);
  cfg->led_hue = fieldValue(doc.getInt("led.hue", cfg->led_hue), def.led_hue);
  cfg->led_saturation = fieldValue(doc.getInt("led.sat", cfg->led_saturation),
                                   def.led_saturation);
  cfg->led_speed =
      fieldValue(doc.getInt("led.spd", cfg->led_speed), def.led_speed);

  // ── cal ──
  cfg->cal_lx = fieldValue(doc.getInt("cal.lx_c", cfg->cal_lx), def.cal_lx);
  cfg->cal_ly = fieldValue(doc.getInt("cal.ly_c", cfg->cal_ly), def.cal_ly);
  cfg->cal_rx = fieldValue(doc.getInt("cal.rx_c", cfg->cal_rx), def.cal_rx);
  cfg->cal_ry = fieldValue(doc.getInt("cal.ry_c", cfg->cal_ry), def.cal_ry);

  LOG_DEBUG("parseProfile: done");
}

uint16_t serializeProfile(const ConfigStore &cfg, char *dst, uint16_t cap) {
  if (!dst || cap == 0) {
    return 0;
  }

  Json doc;
  doc.beginWrite(dst, cap);

  // ── map ──
  doc.printf("{ver:2,map:{socd:%d,four_way:%d,dpad:%d,"
             "inv_x:%d,inv_y:%d,inv_rx:%d,inv_ry:%d,swap:%d,btn_map:[",
             cfg.socd_mode, cfg.four_way_mode, cfg.dpad_mode, cfg.inv_x,
             cfg.inv_y, cfg.inv_rx, cfg.inv_ry, cfg.swap_sticks);
  for (uint8_t i = 0; i < 32; i++) {
    if (i > 0)
      doc.printf(",");
    doc.printf("%d", cfg.btn_map[i]);
  }
  doc.printf("]}");

  // ── stick ──
  doc.printf(",stick:{lx_dz:%d,ly_dz:%d,rx_dz:%d,ry_dz:%d,"
             "lx_sens:%d,ly_sens:%d,rx_sens:%d,ry_sens:%d,curve:%d,ema:%d}",
             cfg.lx_dz, cfg.ly_dz, cfg.rx_dz, cfg.ry_dz, cfg.lx_sens,
             cfg.ly_sens, cfg.rx_sens, cfg.ry_sens, cfg.curve, cfg.ema);

  // ── trig ──
  doc.printf(",trig:{lt_dz:%d,rt_dz:%d}", cfg.lt_dz, cfg.rt_dz);

  // ── led ──
  doc.printf(",led:{bri:%d,mode:%d,hue:%d,sat:%d,spd:%d}", cfg.led_brightness,
             cfg.led_mode, cfg.led_hue, cfg.led_saturation, cfg.led_speed);

  // ── cal ──
  doc.printf(",cal:{lx_c:%d,ly_c:%d,rx_c:%d,ry_c:%d}", cfg.cal_lx, cfg.cal_ly,
             cfg.cal_rx, cfg.cal_ry);

  doc.printf("}"); // close root

  const uint16_t len = static_cast<uint16_t>(doc.end());

  // 0 is the "no body" answer, and there are two ways to get here: the text was
  // cut at the end of dst (frozen reports the length the piece needed, Json
  // clamps it and raises overflowed(), so what sits in dst is a broken prefix),
  // or the body is longer than the flash layer can store. Blanking dst keeps
  // the buffer from looking like a body to a caller that ignores the return
  // value, and every write path in the store rejects a length of 0.
  if (doc.overflowed() || len == 0 || len > Profile::PROFILE_JSON_MAX) {
    LOG_ERROR("serializeProfile: no usable body (cap=%u, len=%u%s)", cap, len,
              doc.overflowed() ? ", truncated" : "");
    dst[0] = '\0';
    return 0;
  }

  return len;
}

} // namespace ThetaGP::Gamepad::Config
