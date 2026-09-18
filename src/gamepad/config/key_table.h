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
  uint8_t flags; // bit0: requiresReboot
};

// flags bit0: a value reaches the key's field, and what the code reading that
// field does with it changes at the next boot on.
inline constexpr uint8_t kKeyFlagRequiresReboot = 0x01;

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

// The keys this build accepts, in the order the protocol lists them. A key the
// table does not carry names no field, so no value can be set through it.
const KeyEntry *keyTable();

// Entries in keyTable().
uint8_t keyTableCount();

// The entry named `key`, or nullptr when the table carries no such key.
const KeyEntry *findKeyEntry(const char *key);

} // namespace ThetaGP::Gamepad::Config
