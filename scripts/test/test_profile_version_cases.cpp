// This file is a part of ThetaGP.
//
// ThetaGP is free software: you can redistribute it
// and/or modify it under the terms of the GNU General
// Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your
// option) any later version.
//
// ThetaGP is distributed in the hope that it will be
// useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE. See the GNU General Public License
// for more details.
//
// You should have received a copy of the GNU General Public
// License along with this program.
//
// If not, see <https://www.gnu.org/licenses/>.

// Cases: a profile body is read at the paths its version carries.
//
// The two bodies below are not written for this test: they are what a board
// carried in Profile 0 (version 1) and Profile 3 (version 2), read back over
// the control protocol. Version 1 kept every domain object inside its one `map`
// object, so a reader that assumes the current paths fetches nothing for those
// fields and every one of them falls back to its default in silence — the
// failure this version check exists to stop. The values in the bodies differ
// from the compiled defaults on purpose (the old body holds bri 50 where the
// default is 128, lx_dz 0 where it is 512), so a field that came back at its
// default is told apart from one that was read.

#include "configs/config_keys.gen.h"
#include "gamepad/config/config_defaults.h"
#include "gamepad/config/config_store.h"
#include "gamepad/config/key_table.h"
#include "utils/json/json.h"
#include "utils/log/log.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using ThetaGP::Gamepad::Config::ConfigStore;
using ThetaGP::Gamepad::Config::kConfigDefaults;
using ThetaGP::Gamepad::Config::KeyEntry;
using ThetaGP::Gamepad::Config::keyTable;
using ThetaGP::Gamepad::Config::keyTableCount;
using ThetaGP::Gamepad::Config::loadKeyElement;
using ThetaGP::Gamepad::Config::parseProfile;
using ThetaGP::Gamepad::Config::serializeProfile;

int g_failed = 0;
int g_ran = 0;

void check(bool ok, const char *what) {
  ++g_ran;
  if (!ok) {
    ++g_failed;
    std::printf("  FAIL %s\n", what);
  }
}

// Profile 0 of the board this was written against, 504 bytes, as read back by
// profile.get. Every domain object sits inside map, and the body still carries
// usb and sys objects whose fields the store no longer has.
constexpr const char *kBodyV1 = R"json({"ver":1,"map":{"socd":0,"four_way":0,"dpad":0,"inv_x":0,"inv_y":0,"inv_rx":0,"inv_ry":0,"swap":0,"btn_map":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31],"stick":{"lx_dz":0,"ly_dz":0,"rx_dz":0,"ry_dz":0,"lx_sens":128,"ly_sens":128,"rx_sens":128,"ry_sens":128,"curve":0,"ema":0},"trig":{"lt_dz":0,"rt_dz":0},"usb":{"poll":1},"led":{"bri":50,"mode":1,"hue":0,"sat":255,"spd":50},"sys":{"log":1,"deb_samp":3,"deb_thr":5},"cal":{"lx_c":0,"ly_c":0,"rx_c":0,"ry_c":0}})json";

// Profile 3 of the same board, 447 bytes: the shape the firmware writes today,
// with the domains at the top level.
constexpr const char *kBodyV2 = R"json({"ver":2,"map":{"socd":0,"four_way":0,"dpad":0,"inv_x":0,"inv_y":0,"inv_rx":0,"inv_ry":0,"swap":0,"btn_map":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31]},"stick":{"lx_dz":0,"ly_dz":0,"rx_dz":0,"ry_dz":0,"lx_sens":128,"ly_sens":128,"rx_sens":128,"ry_sens":128,"curve":0,"ema":0},"trig":{"lt_dz":0,"rt_dz":0},"led":{"bri":63,"mode":1,"hue":0,"sat":255,"spd":50},"cal":{"lx_c":0,"ly_c":0,"rx_c":0,"ry_c":0}})json";

// A body whose version this firmware does not read, carrying values a field of
// the store could hold: what a refusal has to keep out is exactly these.
constexpr const char *kBodyFuture = R"json({"ver":3,"map":{"socd":0},"stick":{"lx_dz":4095},"led":{"bri":9},"cal":{"lx_c":-3000}})json";

// A body that carries no version at all: it predates the field, so it is the
// oldest shape this firmware reads.
constexpr const char *kBodyNoVersion = R"json({"map":{"socd":0,"stick":{"lx_dz":0},"led":{"bri":50}}})json";

// A store holding values no body here carries, so a field a refused read wrote
// is told apart from one it left alone.
ConfigStore markedStore() {
  ConfigStore cfg = kConfigDefaults;
  cfg.lx_dz = 1234;
  cfg.bri = 77;
  cfg.lx_c = -99;
  return cfg;
}

// The text of a serialized body, blanked when the writer refuses.
void bodyText(const ConfigStore &cfg, char *dst, uint16_t cap) {
  std::memset(dst, 0, cap);
  (void)serializeProfile(cfg, dst, cap);
}

uint16_t bodyLen(const char *body) {
  return static_cast<uint16_t>(std::strlen(body));
}

// The firmware's logger hands a line to a callback the platform registers, and
// a host build has no platform behind it. These cases read no log, so the
// logger's entry point takes the line and drops it.
extern "C" void LogPrint(LogLevel level, const char *file, uint16_t line,
                         const char *format, va_list args) {
  (void)level;
  (void)file;
  (void)line;
  (void)format;
  (void)args;
}

} // namespace

int main() {
  std::printf("-- 1. the body a board holds under version 1 reads, not defaults --\n");
  {
    ConfigStore cfg = kConfigDefaults;
    const bool read = parseProfile(kBodyV1, bodyLen(kBodyV1), &cfg);
    check(read, "a version 1 body is read");

    // Each value below is one the old body carries and the compiled default
    // does not: a reader that fetched the current paths would leave the field
    // at the default and these would differ.
    check(cfg.lx_dz == 0,
          "the dead zone comes from map.stick.lx_dz, not the default 512");
    check(cfg.bri == 50, "the brightness comes from map.led.bri, not 128");
    check(cfg.mode == 1, "the LED mode comes from map.led.mode, not 0");
    check(cfg.spd == 50, "the LED speed comes from map.led.spd, not 128");
    check(cfg.hue == 0, "the hue comes from map.led.hue, not the default 180");
    check(cfg.lx_sens == 128, "the sensitivity comes from map.stick.lx_sens");
    check(cfg.socd == 0, "the SOCD mode comes from map.socd, not the default 4");
    check(cfg.btn_map[0] == 0 && cfg.btn_map[31] == 31,
          "the button map comes from map.btn_map");
  }

  std::printf("-- 2. the body the firmware writes today reads at its own paths --\n");
  {
    ConfigStore cfg = kConfigDefaults;
    check(parseProfile(kBodyV2, bodyLen(kBodyV2), &cfg),
          "a version 2 body is read");
    check(cfg.bri == 63, "the brightness comes from led.bri, not 128");
    check(cfg.lx_dz == 0, "the dead zone comes from stick.lx_dz, not 512");
  }

  std::printf("-- 3. what the writer makes of the body it read --\n");
  {
    ConfigStore cfg = kConfigDefaults;
    (void)parseProfile(kBodyV2, bodyLen(kBodyV2), &cfg);

    char body[1024] = {};
    bodyText(cfg, body, sizeof(body));
    check(std::strcmp(body, kBodyV2) == 0,
          "the body read back is written byte for byte as it was carried");
    if (std::strcmp(body, kBodyV2) != 0) {
      std::printf("  wrote: %.80s\n  board: %.80s\n", body, kBodyV2);
    }

    ConfigStore old = kConfigDefaults;
    (void)parseProfile(kBodyV1, bodyLen(kBodyV1), &old);
    char migrated[1024] = {};
    bodyText(old, migrated, sizeof(migrated));
    check(std::strstr(migrated, "{\"ver\":2,") == migrated,
          "a body read under version 1 is written as the current version");
    check(std::strstr(migrated, "\"stick\":{\"lx_dz\":0,") != nullptr,
          "the migrated body carries the old body's values in the new shape");
    check(std::strstr(migrated, "\"usb\"") == nullptr &&
              std::strstr(migrated, "\"sys\"") == nullptr,
          "the objects the store no longer carries are not written back");
  }

  std::printf("-- 4. a version this firmware does not read is refused whole --\n");
  {
    ConfigStore cfg = markedStore();
    check(!parseProfile(kBodyFuture, bodyLen(kBodyFuture), &cfg),
          "a version 3 body is refused");
    check(cfg.lx_dz == 1234 && cfg.bri == 77 && cfg.lx_c == -99,
          "no field of a refused body was written");

    ConfigStore zero = markedStore();
    const char *kVersionZero = R"json({"ver":0,"stick":{"lx_dz":4095}})json";
    check(!parseProfile(kVersionZero, bodyLen(kVersionZero), &zero),
          "a version 0 body is refused");
    check(zero.lx_dz == 1234, "no field of it was written either");
  }

  std::printf("-- 5. a body with no version is the oldest shape --\n");
  {
    ConfigStore cfg = kConfigDefaults;
    check(parseProfile(kBodyNoVersion, bodyLen(kBodyNoVersion), &cfg),
          "a body carrying no version is read");
    check(cfg.lx_dz == 0, "its fields are read at the oldest shape's paths");
    check(cfg.bri == 50, "and its later fields are read there too");
  }

  std::printf("-- 6. a value outside the range a row declares --\n");
  {
    const char *kOutOfRange = R"json({"ver":2,"map":{"dpad":5},"led":{"bri":250}})json";
    ConfigStore cfg = kConfigDefaults;
    (void)parseProfile(kOutOfRange, bodyLen(kOutOfRange), &cfg);
    check(cfg.dpad == kConfigDefaults.dpad,
          "a D-pad mode outside 0..2 leaves the field at its default");
    check(cfg.bri == 250, "a brightness inside its range is stored");
  }

  std::printf("%d checks, %d failed\n", g_ran, g_failed);
  return g_failed == 0 ? 0 : 1;
}
