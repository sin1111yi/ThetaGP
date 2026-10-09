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
// One struct per domain the key table names a field under. A domain's fields
// are the fields a profile body carries under that name, and the store is one
// member per domain: the byte a key stands for is its domain's offset plus its
// own offset inside it.
struct Map {
  uint8_t btn_map[32]; // Physical key → button bit index (0xFF = unmapped)
  uint8_t socd;
  uint8_t four_way;
  uint8_t dpad;
  uint8_t inv_x;
  uint8_t inv_y;
  uint8_t inv_rx;
  uint8_t inv_ry;
  uint8_t swap;
};

struct Stick {
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
};

struct Trig {
  uint8_t lt_dz;
  uint8_t rt_dz;
};

struct Rgb {
  // Key `led.rgb.hz`: the frames a second the strip is refreshed at.
  uint8_t hz;
};

struct Led {
  uint8_t bri;
  uint8_t mode;
  uint16_t hue;
  uint8_t sat;
  Rgb rgb;
};

struct Usb {
  // Key `usb.input_mode`: the mode the device reports as. It is 16 bits wide
  // because the store has to tile without a hole: a single byte here would end
  // the store on an odd count and the alignment would open one the table could
  // not claim.
  uint16_t input_mode;
};

struct Cal {
  int16_t lx_c;
  int16_t ly_c;
  int16_t rx_c;
  int16_t ry_c;
};

// Every field of the store, grouped by the domain its key is named under, in
// the order the key table lists those domains in.
struct ConfigStore {
  Map map;
  Stick stick;
  Trig trig;
  Led led;
  Usb usb;
  Cal cal;
};

// The domains tile the store, and each one starts where the one before it
// ended: a domain or a field added here without the row that names it leaves a
// byte no profile key has a path for, which is what the table's own tiling
// assertion counts on.
static_assert(sizeof(ConfigStore) ==
                  sizeof(Map) + sizeof(Stick) + sizeof(Trig) + sizeof(Led) +
                      sizeof(Usb) + sizeof(Cal),
              "config store: the domains do not tile the store");

// Parse the JSON profile body `json[0 .. len)` into ConfigStore. `len` is the
// body's length in bytes, and it is what bounds the parse: the body needs no
// terminator, and no byte behind it is read even when it fills the buffer it
// sits in whole. Pass the length the store reported for the body being parsed.
//
// Answers whether the body was read. False means the body's version is not one
// this firmware reads, and nothing of that body was applied: a version it cannot
// place is refused whole rather than read at paths that hold nothing. What the
// store holds after a refusal is the caller's business — both callers hand in
// the compiled defaults.
bool parseProfile(const char *json, uint32_t len, ConfigStore *cfg);

// Write cfg into dst as a profile JSON body. `cap` is the size of dst in bytes,
// terminator included. Returns the body length in bytes, terminator not
// counted, or 0 when no usable body was produced: dst was too small for it, or
// the body would be longer than PROFILE_JSON_MAX, the most the flash layer
// stores. A truncated body is never returned — 0 means "nothing to persist", so
// a caller must not hand that length to the store.
uint16_t serializeProfile(const ConfigStore &cfg, char *dst, uint16_t cap);

} // namespace ThetaGP::Gamepad::Config
