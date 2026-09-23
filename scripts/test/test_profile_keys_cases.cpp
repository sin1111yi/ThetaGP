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

// Cases: the key table and a profile body name the same field the same way.
//
// The table maps a protocol key name to the path a body carries for the same
// field, and the body's reader and writer take that path from the table. These
// cases hold the two ends together without a board: the writer's text is read
// back through the table's paths, a body written under those paths fills the
// fields, and a control shows a body that carries a protocol key name instead
// leaves the field alone.
//
// The names the stored format has always carried are pinned by text, because a
// profile written by an older firmware has to keep being read.

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
using ThetaGP::Gamepad::Config::kBtnMapKey;
using ThetaGP::Gamepad::Config::kConfigDefaults;
using ThetaGP::Gamepad::Config::KeyEntry;
using ThetaGP::Gamepad::Config::keyTable;
using ThetaGP::Gamepad::Config::KeyTableKey;
using ThetaGP::Gamepad::Config::KeyType;
using ThetaGP::Gamepad::Config::kFourWayKey;
using ThetaGP::Gamepad::Config::kSocdModeKey;
using ThetaGP::Gamepad::Config::parseProfile;
using ThetaGP::Gamepad::Config::profileLeafName;
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

// The body the defaults serialize to, and the same body parsed back.
constexpr uint16_t kBodyCap = 1024;

// A value no field of the store holds, so a path that names nothing is told
// apart from a path that names a field holding zero.
constexpr int kNoValue = -123456;

uint16_t writeDefaults(char *dst, uint16_t cap) {
  return serializeProfile(kConfigDefaults, dst, cap);
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
  std::printf("-- 1. the stored format's names are what the writer emits --\n");
  {
    char body[kBodyCap] = {};
    const uint16_t len = writeDefaults(body, sizeof(body));
    check(len > 0, "the defaults serialize to a body");
    if (std::strstr(body, "{\"ver\":2,\"map\":{\"socd\":") != body) {
      std::printf("  body starts: %.48s\n", body);
    }
    check(std::strstr(body, "{\"ver\":2,\"map\":{\"socd\":") == body,
          "the map object opens with the name the stored format carries");
    check(std::strstr(body, "\"four_way\":") != nullptr,
          "the four way key keeps the name the stored format carries");
    check(std::strstr(body, "\"btn_map\":[") != nullptr,
          "the button map keeps the name the stored format carries");
    check(std::strstr(body, "socd_mode:") == nullptr &&
              std::strstr(body, "four_way_mode:") == nullptr,
          "no protocol key name reaches the body");
  }

  std::printf("-- 2. every row's path names a field of the body it wrote --\n");
  {
    char body[kBodyCap] = {};
    const uint16_t len = writeDefaults(body, sizeof(body));
    Json doc;
    doc.parse(body, static_cast<int>(len));

    const KeyEntry &socd = keyTable()[kSocdModeKey];
    const KeyEntry &fourWay = keyTable()[kFourWayKey];
    const KeyEntry &btnMap = keyTable()[kBtnMapKey];

    check(doc.getInt(socd.jsonKey, kNoValue) == kConfigDefaults.socd_mode,
          "the SOCD mode key's path reads the value the field holds");
    check(doc.getInt(fourWay.jsonKey, kNoValue) ==
              kConfigDefaults.four_way_mode,
          "the four way key's path reads the value the field holds");

    const int arrLen = doc.getArrLen(btnMap.jsonKey);
    check(arrLen == 32, "the button map key's path names the array");
    bool same = arrLen == 32;
    for (int i = 0; same && i < 32; i++) {
      same = doc.getArrInt(btnMap.jsonKey, i, kNoValue) ==
             kConfigDefaults.btn_map[i];
    }
    check(same, "every element the path names holds what the field holds");
  }

  std::printf("-- 3. a body under those paths fills the fields --\n");
  {
    // The three keys carry values that differ from the defaults, so a field
    // left untouched is told apart from one the body reached.
    char body[kBodyCap] = {};
    const int socdWanted = 2;
    const int fourWayWanted = 1;
    std::snprintf(body, sizeof(body), "{ver:2,map:{%s:%d,%s:%d,%s:[0,0]}}",
                  profileLeafName(keyTable()[kSocdModeKey]), socdWanted,
                  profileLeafName(keyTable()[kFourWayKey]), fourWayWanted,
                  profileLeafName(keyTable()[kBtnMapKey]));
    // The array's slots beyond the two the body carries take the sentinel.
    ConfigStore cfg = kConfigDefaults;
    parseProfile(body, static_cast<uint32_t>(std::strlen(body)), &cfg);
    check(cfg.socd_mode == socdWanted,
          "the SOCD mode field takes the value the body carries");
    check(cfg.four_way_mode == fourWayWanted,
          "the four way field takes the value the body carries");
    check(cfg.btn_map[0] == 0 && cfg.btn_map[1] == 0,
          "the array's elements take the values the body carries");
    check(cfg.btn_map[31] == 0xFF,
          "the array's slots the body does not reach take the sentinel");
  }

  std::printf(
      "-- 4. control: a protocol key name in a body fills nothing --\n");
  {
    char body[kBodyCap] = {};
    std::snprintf(body, sizeof(body), "{ver:2,map:{map.socd_mode:%d}}",
                  (kConfigDefaults.socd_mode + 1) % 2);
    ConfigStore cfg = kConfigDefaults;
    parseProfile(body, static_cast<uint32_t>(std::strlen(body)), &cfg);
    check(cfg.socd_mode == kConfigDefaults.socd_mode,
          "a body naming the protocol key leaves the field at its default");
  }

  std::printf("%d checks, %d failed\n", g_ran, g_failed);
  return g_failed == 0 ? 0 : 1;
}
