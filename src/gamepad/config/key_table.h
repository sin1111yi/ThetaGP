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

#include <cstddef>
#include <cstdint>

namespace ThetaGP::Gamepad::Config {

// How a value of a key travels on the wire. U8Array is a run of count bytes,
// each one an element of its own.
enum class KeyType : uint8_t { U8, U16, I16, U8Array };

// One settable key: the name the protocol carries, the name a profile body
// carries for the same field, the field of ConfigStore it stands for, and the
// values that field accepts. count is the field's element count — 1 for a
// scalar, the number of elements for an array.
struct KeyEntry {
  const char *key;     // protocol key name
  const char *jsonKey; // profile JSON path
  uint16_t offset;     // offsetof(ConfigStore, field)
  KeyType type;
  uint8_t count; // element count for array keys, 1 otherwise
  int32_t minVal;
  int32_t maxVal;
  uint8_t flags; // bit0: requiresReboot, bit1: acceptsUnmapped
};

// flags bit0: a value reaches the key's field, and what the code reading that
// field does with it changes at the next boot on.
inline constexpr uint8_t kKeyFlagRequiresReboot = 0x01;

// flags bit1: the key's field also holds the unmapped sentinel beside the
// minVal..maxVal range, so the sentinel is an accepted value even though it
// lies outside that range. A field whose elements each name a destination is
// where this holds: the element of a destination nothing was assigned to
// carries the sentinel, and a caller has to be able to set it back.
inline constexpr uint8_t kKeyFlagAcceptsUnmapped = 0x02;

// Bytes one element of a value of this type takes. A type outside the enum
// takes none.
constexpr uint8_t keyTypeWidth(KeyType type) {
  switch (type) {
  case KeyType::U8:
  case KeyType::U8Array:
    return 1;
  case KeyType::U16:
  case KeyType::I16:
    return 2;
  }
  return 0;
}

// Most entries the table may carry, and the longest a key name may be. The key
// list reply of the control protocol is built in a buffer sized from both, so
// growing the table past the count or a name past the length is a build error
// rather than a reply cut short at run time.
inline constexpr uint8_t kKeyTableMaxEntries = 12;
inline constexpr size_t kKeyTableMaxNameLen = 24;

// The keys this build accepts, in the order the protocol lists them. A key the
// table does not carry names no field, so no value can be set through it.
const KeyEntry *keyTable();

// Entries in keyTable().
uint8_t keyTableCount();

// The entry named `key`, or nullptr when the table carries no such key.
const KeyEntry *findKeyEntry(const char *key);

} // namespace ThetaGP::Gamepad::Config
