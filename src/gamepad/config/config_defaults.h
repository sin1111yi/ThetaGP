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
    cfg.btn_map[slot] = kBtnMap[slot];
  }
  cfg.socd     = kKeyDefaultSocd;
  cfg.four_way = kKeyDefaultFourWay;
  cfg.dpad     = kKeyDefaultDpad;
  cfg.inv_x    = kKeyDefaultInvX;
  cfg.inv_y    = kKeyDefaultInvY;
  cfg.inv_rx   = kKeyDefaultInvRx;
  cfg.inv_ry   = kKeyDefaultInvRy;
  cfg.swap     = kKeyDefaultSwap;

  // ── Stick settings ──
  cfg.lx_dz   = kKeyDefaultLxDz;
  cfg.ly_dz   = kKeyDefaultLyDz;
  cfg.rx_dz   = kKeyDefaultRxDz;
  cfg.ry_dz   = kKeyDefaultRyDz;
  cfg.lx_sens = kKeyDefaultLxSens;
  cfg.ly_sens = kKeyDefaultLySens;
  cfg.rx_sens = kKeyDefaultRxSens;
  cfg.ry_sens = kKeyDefaultRySens;
  cfg.curve   = kKeyDefaultCurve;
  cfg.ema     = kKeyDefaultEma;

  // ── Trigger settings ──
  cfg.lt_dz = kKeyDefaultLtDz;
  cfg.rt_dz = kKeyDefaultRtDz;

  // ── LED settings ──
  cfg.bri  = kKeyDefaultBri;
  cfg.mode = kKeyDefaultMode;
  cfg.hue  = kKeyDefaultHue;
  cfg.sat  = kKeyDefaultSat;
  cfg.hz   = kKeyDefaultHz;

  // ── Calibration ──
  cfg.lx_c = kKeyDefaultLxC;
  cfg.ly_c = kKeyDefaultLyC;
  cfg.rx_c = kKeyDefaultRxC;
  cfg.ry_c = kKeyDefaultRyC;

  return cfg;
}

} // namespace detail

// The compiled-in ConfigStore defaults.
inline constexpr ConfigStore kConfigDefaults = detail::makeDefaults();

} // namespace ThetaGP::Gamepad::Config

#endif // BDCFG_KEYPAD_BUTTON_MAP
