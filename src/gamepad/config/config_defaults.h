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
#include "configs/config_keys.gen.h"
#include "gamepad/config/config_store.h"

// GAMEPAD_MASK_*, which BDCFG_KEYPAD_BUTTON_MAP is written in terms of.
#include "gamepad/gamepad_state.h"

#include <array>
#include <cstdint>

// The button table below comes from the board's key table, so a board that
// configures no table fails the build here.
#ifndef BDCFG_KEYPAD_BUTTON_MAP
#error "[keypad] button_map is required — see configs/CONFIGURATION.md"
#else

namespace ThetaGP::Gamepad::Config {
namespace detail {

// Slot value for a physical key that carries no button. It is the same
// sentinel parseProfile writes into the slots a profile does not fill.
inline constexpr uint8_t kBtnMapUnmapped = 0xFF;

// Physical key slots the button table covers.
inline constexpr uint8_t kBtnMapSlots = 32;

// Bits a button mask carries. A mask names one gamepad button per bit, so the
// bit indexes it can name are 0 .. kBtnMaskBits - 1.
inline constexpr uint8_t kBtnMaskBits = 32;

// Bit index of a one-hot button mask, the form every entry of the board table
// has. A mask that is not one-hot reaches no bit and falls through to the
// sentinel, which the static_assert below rejects; the conversion runs at
// compile time, once per mapping entry.
constexpr uint16_t bitIndexOf(uint32_t mask) {
  for (uint16_t bit = 0; bit < kBtnMaskBits; ++bit) {
    if (mask == (1U << bit)) {
      return bit;
    }
  }
  return kBtnMapUnmapped;
}

// The board's key table in the shape the generator emits it: physical key and
// the gamepad button mask that key carries.
struct KeyMapping {
  uint8_t key;
  uint32_t mask;
};

inline constexpr KeyMapping kKeypadButtonMap[] = {BDCFG_KEYPAD_BUTTON_MAP};

// Physical key index -> button bit index, the table read() consumes. The table
// says "unmapped" by leaving a key out, so a key the board does not list keeps
// the sentinel and every slot the entries miss is filled here. An entry whose
// key is outside the table is skipped rather than written — keysAddressSlots()
// below rejects it at compile time.
constexpr std::array<uint8_t, kBtnMapSlots> buildBtnMap() {
  std::array<uint8_t, kBtnMapSlots> map{};
  for (uint8_t &slot : map) {
    slot = kBtnMapUnmapped;
  }
  for (const KeyMapping &entry : kKeypadButtonMap) {
    if (entry.key < kBtnMapSlots) {
      map[entry.key] = bitIndexOf(entry.mask);
    }
  }
  return map;
}

// Every entry must name a real button position: a mask that is not one-hot has
// no bit index, and an index outside the mask width names no button the report
// can carry.
constexpr bool entriesAreButtonBits() {
  for (const KeyMapping &entry : kKeypadButtonMap) {
    if (bitIndexOf(entry.mask) >= kBtnMaskBits) {
      return false;
    }
  }
  return true;
}

// A physical key must address a slot of the generated table.
constexpr bool keysAddressSlots() {
  for (const KeyMapping &entry : kKeypadButtonMap) {
    if (entry.key >= kBtnMapSlots) {
      return false;
    }
  }
  return true;
}

static_assert(entriesAreButtonBits(),
              "BDCFG_KEYPAD_BUTTON_MAP: each mask must be a single button bit");
static_assert(keysAddressSlots(),
              "BDCFG_KEYPAD_BUTTON_MAP: physical key index must be below kBtnMapSlots");

inline constexpr std::array<uint8_t, kBtnMapSlots> kBtnMap = buildBtnMap();

// The default set as a whole: each field's factory default, emitted from the
// config key declaration, plus the board's key table. The fields are written
// one by one in declaration order; the button table is a compile-time
// conversion of the board macro.
constexpr ConfigStore makeDefaults() {
  ConfigStore cfg{};

  // ── Map settings ──
  for (uint8_t slot = 0; slot < kBtnMapSlots; ++slot) {
    cfg.map.btn_map[slot] = kBtnMap[slot];
  }
  cfg.map.socd     = kKeyDefaultMapSocd;
  cfg.usb.input_mode = kKeyDefaultUsbInputMode;
  cfg.map.four_way = kKeyDefaultMapFourWay;
  cfg.map.dpad     = kKeyDefaultMapDpad;
  cfg.map.inv_x    = kKeyDefaultMapInvX;
  cfg.map.inv_y    = kKeyDefaultMapInvY;
  cfg.map.inv_rx   = kKeyDefaultMapInvRx;
  cfg.map.inv_ry   = kKeyDefaultMapInvRy;
  cfg.map.swap     = kKeyDefaultMapSwap;

  // ── Stick settings ──
  cfg.stick.lx_dz   = kKeyDefaultStickLxDz;
  cfg.stick.ly_dz   = kKeyDefaultStickLyDz;
  cfg.stick.rx_dz   = kKeyDefaultStickRxDz;
  cfg.stick.ry_dz   = kKeyDefaultStickRyDz;
  cfg.stick.lx_sens = kKeyDefaultStickLxSens;
  cfg.stick.ly_sens = kKeyDefaultStickLySens;
  cfg.stick.rx_sens = kKeyDefaultStickRxSens;
  cfg.stick.ry_sens = kKeyDefaultStickRySens;
  cfg.stick.curve   = kKeyDefaultStickCurve;
  cfg.stick.ema     = kKeyDefaultStickEma;

  // ── Trigger settings ──
  cfg.trig.lt_dz = kKeyDefaultTrigLtDz;
  cfg.trig.rt_dz = kKeyDefaultTrigRtDz;

  // ── LED settings ──
  cfg.led.bri  = kKeyDefaultLedBri;
  cfg.led.mode = kKeyDefaultLedMode;
  cfg.led.hue  = kKeyDefaultLedHue;
  cfg.led.sat  = kKeyDefaultLedSat;
  cfg.led.rgb.hz   = kKeyDefaultLedRgbHz;

  // ── Calibration ──
  cfg.cal.lx_c = kKeyDefaultCalLxC;
  cfg.cal.ly_c = kKeyDefaultCalLyC;
  cfg.cal.rx_c = kKeyDefaultCalRxC;
  cfg.cal.ry_c = kKeyDefaultCalRyC;

  return cfg;
}

} // namespace detail

// The compiled-in ConfigStore defaults.
inline constexpr ConfigStore kConfigDefaults = detail::makeDefaults();

} // namespace ThetaGP::Gamepad::Config

#endif // BDCFG_KEYPAD_BUTTON_MAP
