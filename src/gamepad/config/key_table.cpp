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

// The keys themselves: the declaration in configs/config_keys.toml expanded.
#include "configs/config_keys.gen.h"

// The mask a button bit index is drawn from.
#include "gamepad/config/config_defaults.h"
#include "gamepad/gamepad_enums.h"

#include <cstddef>
#include <cstring>

namespace ThetaGP::Gamepad::Config {
namespace {

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

// The value a `value` field that is absent, or spelled as anything but a plain
// integer, arrives as. No entry may accept it: a key whose range reached it
// would take a value the caller never sent.
constexpr bool rangesExcludeWireSentinel() {
  for (const KeyEntry &entry : kKeyTable) {
    if (entry.minVal == INT32_MIN) {
      return false;
    }
  }
  return true;
}

static_assert(rangesExcludeWireSentinel(),
              "key table: a key accepts the value a missing value reads as");

// The key list reply of the control protocol is sized for kKeyTableMaxEntries
// entries of kKeyTableMaxNameLen bytes of key name each. Both limits are held
// here, so the reply cannot meet a table it has no room for.
constexpr bool entriesWithinListLimits() {
  if (kKeyTableCount > kKeyTableMaxEntries) {
    return false;
  }
  for (const KeyEntry &entry : kKeyTable) {
    size_t nameLen = 0;
    while (entry.key[nameLen] != '\0') {
      ++nameLen;
    }
    if (nameLen > kKeyTableMaxNameLen) {
      return false;
    }
  }
  return true;
}

static_assert(entriesWithinListLimits(),
              "key table: the key list reply is not sized for this table");

// The SOCD mode holds a SOCDMode enumerator, so that enum bounds the key's
// range. The two are held together here, so an enumerator added to the enum
// either widens the declared range or fails this build.
static_assert(keyEntry(ConfigKey::Socd).maxVal ==
                  static_cast<int32_t>(Enums::SOCDMode::Count) - 1,
              "key table: the SOCD mode key does not cover the SOCD modes");

// A profile body carries a key's profile name through the JSON writer, which
// formats a name of up to kJsonWriterNameBufLen bytes on its stack: a longer
// one makes it allocate a buffer it then frees.
constexpr int kJsonWriterNameBufLen = 20;
constexpr bool leavesWithinWriterBuffer() {
  for (const KeyEntry &entry : kKeyTable) {
    int len = 0;
    for (const char *p = profileLeafName(entry); *p != '\0'; ++p) {
      ++len;
    }
    if (len >= kJsonWriterNameBufLen) {
      return false;
    }
  }
  return true;
}

static_assert(leavesWithinWriterBuffer(),
              "key table: a profile key name takes a heap buffer to write");

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
