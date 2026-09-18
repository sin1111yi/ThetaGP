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

#include "gamepad/config/key_table.h"

// The mask a button bit index is drawn from.
#include "gamepad/config/config_defaults.h"
#include "gamepad/gamepadenums.h"

#include <cstddef>
#include <cstring>

namespace ThetaGP::Gamepad::Config {
namespace {

// A key stands for one field of the store under two names: `key` is the name a
// caller sends, `jsonKey` is the path a profile body carries for that same
// field. The table carries the keys the running code reads — a key whose field
// nothing reads takes a value that changes nothing.
constexpr KeyEntry kKeyTable[] = {
    {"map.socd_mode", "map.socd", offsetof(ConfigStore, socd_mode), KeyType::U8,
     1, 0, static_cast<int32_t>(Enums::SOCDMode::Count) - 1, 0},
    {"map.four_way", "map.four_way", offsetof(ConfigStore, four_way_mode),
     KeyType::U8, 1, 0, 1, 0},
    {"map.btn_map", "map.btn_map", offsetof(ConfigStore, btn_map),
     KeyType::U8Array, static_cast<uint8_t>(sizeof(ConfigStore::btn_map)), 0,
     detail::kBtnMaskBits - 1, 0},
};

constexpr uint8_t kKeyTableCount =
    static_cast<uint8_t>(sizeof(kKeyTable) / sizeof(kKeyTable[0]));

// Every entry names bytes of the store, and a value read or written through it
// has to stay inside the store. The last field of the store is the strictest
// case, so the check walks the whole table.
constexpr bool entriesStayInStore() {
  for (const KeyEntry &entry : kKeyTable) {
    const size_t end =
        static_cast<size_t>(entry.offset) +
        static_cast<size_t>(keyTypeWidth(entry.type)) * entry.count;
    if (end > sizeof(ConfigStore)) {
      return false;
    }
  }
  return true;
}

static_assert(entriesStayInStore(),
              "key table: an entry runs past ConfigStore");

// The table carries the keys the code reads, and the three map keys are those.
static_assert(kKeyTableCount == 3, "key table: the connected keys are three");

} // namespace

const KeyEntry *keyTable() { return kKeyTable; }

uint8_t keyTableCount() { return kKeyTableCount; }

const KeyEntry *findKeyEntry(const char *key) {
  if (!key) {
    return nullptr;
  }
  for (uint8_t i = 0; i < kKeyTableCount; ++i) {
    if (std::strcmp(kKeyTable[i].key, key) == 0) {
      return &kKeyTable[i];
    }
  }
  return nullptr;
}

} // namespace ThetaGP::Gamepad::Config
