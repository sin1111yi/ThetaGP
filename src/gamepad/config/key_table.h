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

#include "gamepad/config/config_store.h"
#include "pb/ThetaGP.pb.h"

#include <cstddef>
#include <cstdint>

namespace ThetaGP::Gamepad::Config {

// The shape a value travels in, as the schema declares it. The table below is
// indexed by it.
using KeyType = ThetaGP_ConfigKeyType;

// One past the last shape; the schema declares no count of its own.
inline constexpr uint8_t kKeyTypeCount = ThetaGP_ConfigKeyType_CONFIG_KEY_U8_ARRAY + 1;

// One field of the store: its name, the field of ConfigStore it stands for, and
// the values that field accepts. The name is the whole identity of the field:
// a caller sends it, and a profile body carries it as a path.
struct KeyEntry {
  const char *key; // the field's name
  uint16_t offset; // offsetof(ConfigStore, field)
  KeyType type;
  uint8_t count; // element count for array keys, 1 otherwise
  int32_t minVal;
  int32_t maxVal;
  uint8_t flags; // bit0: requiresReboot, bit1: acceptsUnmapped, bit2: exposed
  // The schema enum the value is drawn from, by its name, or "" for a plain
  // number.
  const char *valueEnum;
};

// flags bit0: the field is read at boot, so a value reaches it at the next one.
inline constexpr uint8_t kKeyFlagRequiresReboot = 0x01;

// flags bit1: the field also holds the unmapped sentinel, which lies outside
// the minVal..maxVal range and is accepted all the same.
inline constexpr uint8_t kKeyFlagAcceptsUnmapped = 0x02;

// flags bit2: the key is one the control protocol accepts. The table spans the
// whole store; a profile body is read and written through it.
inline constexpr uint8_t kKeyFlagExposed = 0x04;

// True when the control protocol accepts the key of this row.
constexpr bool isKeyExposed(const KeyEntry &entry) {
  return (entry.flags & kKeyFlagExposed) != 0;
}

// Bytes one element of each type takes, in the order the enum lists them.
constexpr uint8_t kKeyTypeWidths[] = {1, 2, 2, 1};

static_assert(sizeof(kKeyTypeWidths) == kKeyTypeCount,
              "key table: a key type carries no width");

// Bytes one element of a value of this type takes. A type outside the enum
// takes none.
constexpr uint8_t keyTypeWidth(KeyType type) {
  const size_t index = static_cast<size_t>(type);
  return index < sizeof(kKeyTypeWidths) ? kKeyTypeWidths[index] : 0;
}

// Most rows the key list reply carries, and the longest a key name may be. The
// reply is built in a buffer sized from both.
inline constexpr uint8_t kKeyTableMaxEntries = 16;
inline constexpr size_t kKeyTableMaxNameLen = 24;

// Every field of ConfigStore, in the order the declaration lists them. A field
// with no row is not carried by a profile body.
const KeyEntry *keyTable();

// Entries in keyTable().
uint8_t keyTableCount();

// The entry named `key` in the whole table, or nullptr.
const KeyEntry *findKeyEntry(const char *key);

// The entry named `key` among the rows the control protocol accepts, or
// nullptr.
const KeyEntry *findExposedKeyEntry(const char *key);

// Reads element `index` of the row's field out of `cfg` as a signed integer.
int32_t loadKeyElement(const ConfigStore &cfg, const KeyEntry &entry,
                       uint16_t index);

// Writes `value` as element `index` of the row's field in `cfg`, in the width
// the row's type carries.
void storeKeyElement(ConfigStore &cfg, const KeyEntry &entry, uint16_t index,
                     int32_t value);

// True when `value` is one the row accepts as a single element: inside
// minVal..maxVal, or the unmapped sentinel on a row that carries
// kKeyFlagAcceptsUnmapped.
bool keyElementAccepted(const KeyEntry &entry, int32_t value);

// The name a profile body carries for a key inside its parent object: the tail
// of the key's name past its last dot.
constexpr const char *profileLeafName(const KeyEntry &entry) {
  const char *leaf = entry.key;
  for (const char *p = entry.key; *p != '\0'; ++p) {
    if (*p == '.') {
      leaf = p + 1;
    }
  }
  return leaf;
}

} // namespace ThetaGP::Gamepad::Config
