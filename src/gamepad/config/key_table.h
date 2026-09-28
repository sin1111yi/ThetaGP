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
// each one an element of its own. Count closes the list: it stands for no type,
// and it is what holds the widths below to the same length as this enum.
enum class KeyType : uint8_t { U8, U16, I16, U8Array, Count };

// One field of the store: its name, the field of ConfigStore it stands for, and
// the values that field accepts. The name is the whole identity of the field —
// what a caller sends and the path a profile body carries are this same string
// — and the C++ side of the field is its last segment. count is the field's
// element count: 1 for a scalar, the number of elements for an array.
struct KeyEntry {
  const char *key; // the field's name
  uint16_t offset; // offsetof(ConfigStore, field)
  KeyType type;
  uint8_t count; // element count for array keys, 1 otherwise
  int32_t minVal;
  int32_t maxVal;
  uint8_t flags; // bit0: requiresReboot, bit1: acceptsUnmapped, bit2: exposed
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

// flags bit2: the row is a key the control protocol accepts. The table spans
// the whole store, because a profile body is read and written through it, and
// this bit is what separates the fields a caller may reach from the fields only
// a profile carries.
inline constexpr uint8_t kKeyFlagExposed = 0x04;

// True when the control protocol accepts the key of this row.
constexpr bool isKeyExposed(const KeyEntry &entry) {
  return (entry.flags & kKeyFlagExposed) != 0;
}

// Bytes one element of a value of this type takes, in the order the enum lists
// the types. The two lengths are held together, so a type added to the enum
// without a width here does not build.
constexpr uint8_t kKeyTypeWidths[] = {1, 2, 2, 1};

static_assert(sizeof(kKeyTypeWidths) == static_cast<size_t>(KeyType::Count),
              "key table: a key type carries no width");

// Bytes one element of a value of this type takes. A type outside the enum
// takes none.
constexpr uint8_t keyTypeWidth(KeyType type) {
  const size_t index = static_cast<size_t>(type);
  return index < sizeof(kKeyTypeWidths) ? kKeyTypeWidths[index] : 0;
}

// Most rows the key list reply may carry, and the longest a key name may be.
// The reply lists the exposed rows of the table, and it is built in a buffer
// sized from both, so a table whose exposed rows outgrow the count — or a name
// that outgrows the length — is a build error rather than a reply cut short at
// run time.
inline constexpr uint8_t kKeyTableMaxEntries = 12;
inline constexpr size_t kKeyTableMaxNameLen = 24;

// Every field of ConfigStore, in the order the declaration lists them. A field
// with no row here is a field a profile body carries nothing for, so it reads
// and is written as nothing.
const KeyEntry *keyTable();

// Entries in keyTable().
uint8_t keyTableCount();

// The entry named `key`, or nullptr when the table carries no such key. Both
// lookups are over the whole table: a field a profile reaches but no command
// does is still a field, and this one finds it.
const KeyEntry *findKeyEntry(const char *key);

// The entry named `key` among the rows the control protocol accepts, or nullptr
// when no exposed row carries that name. A command naming a key uses this one,
// so a field no command reaches is refused as an unknown key rather than
// answered as one this firmware carries.
const KeyEntry *findExposedKeyEntry(const char *key);

// Reads element `index` of the row's field out of `cfg` as a signed integer.
// The element is one of the field's own type, read at the row's offset: the
// offset is added here and nowhere else, so a value reaches the field its row
// names and no other.
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
// of the key's name past its last dot, so a body writes the name `map.socd` as
// `socd` inside its `map` object.
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
