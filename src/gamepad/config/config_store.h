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

#include <cstddef>

#include "build_info.h"

#include <cstdint>

namespace ThetaGP::Gamepad::Config {

// The `map` domain of a profile body. No field carries an initializer: the
// defaults are aggregated in config_defaults.h, so a store nothing has assigned to
// is zero rather than a usable configuration. A key's byte is its domain's offset
// plus its own offset inside it.
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

struct Sys {
  // Key `sys.log`: the threshold the firmware logs at.
  uint8_t log;
  // Key `sys.deb_samp`: scans a press holds before it commits.
  uint8_t deb_samp;
  // Key `sys.deb_thr`: scans a release holds before it commits.
  uint8_t deb_thr;
};

struct Usb {
  // Key `usb.input_mode`: the mode the device reports as.
  uint8_t input_mode;
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
  Cal cal;
  Usb usb;
  Sys sys;
};

// The domains sit one after another, so a domain or a field added without the row
// that names it fails here.
static_assert(offsetof(ConfigStore, stick) == sizeof(Map), "config store: Map is not first and whole");
static_assert(offsetof(ConfigStore, trig) ==
                  offsetof(ConfigStore, stick) + sizeof(Stick),
              "config store: Stick does not start where Map ended");
static_assert(offsetof(ConfigStore, led) ==
                  offsetof(ConfigStore, trig) + sizeof(Trig),
              "config store: Trig does not start where Stick ended");
static_assert(offsetof(ConfigStore, cal) ==
                  offsetof(ConfigStore, led) + sizeof(Led),
              "config store: Led does not start where Trig ended");
static_assert(offsetof(ConfigStore, usb) ==
                  offsetof(ConfigStore, cal) + sizeof(Cal),
              "config store: Cal does not start where Led ended");
static_assert(offsetof(ConfigStore, sys) ==
                  offsetof(ConfigStore, usb) + sizeof(Usb),
              "config store: Usb does not start where Cal ended");
static_assert(sizeof(ConfigStore) - offsetof(ConfigStore, sys) - sizeof(Sys) <
                  alignof(ConfigStore),
              "config store: the store ends in more than its own padding");

// Reads the JSON body `json[0 .. len)` into cfg. `len` bounds the parse: the body
// needs no terminator, and no byte behind it is read. Answers false for a version
// this firmware does not read, with nothing of that body applied.
bool parseProfile(const char *json, uint32_t len, ConfigStore *cfg);

// Writes cfg into dst as a profile body, with `name` beside the version; an empty
// name writes none. `cap` includes the terminator. Answers the body's length, or 0
// when dst is too small or the body would pass PROFILE_JSON_MAX: a truncated body
// is never returned, so a caller never hands 0 to the store.

// The longest name a profile body carries, terminator included.
inline constexpr uint16_t kProfileNameMax = 25;

// The version of the body format this firmware writes, and the oldest one it
// still reads.
inline constexpr int kProfileVersion = 3;
inline constexpr int kOldestReadableVersion = 1;

uint16_t serializeProfile(const ConfigStore &cfg, const char *name, char *dst,
                          uint16_t cap);

} // namespace ThetaGP::Gamepad::Config
