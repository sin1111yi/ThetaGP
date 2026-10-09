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

// The fields themselves: the declaration in configs/config_keys.toml expanded.
#include "configs/config_keys.gen.h"

// The sentinel a button slot with no key assigned carries.
#include "gamepad/config/config_defaults.h"
#include "pb/enums.pb.h"

#include <cstddef>
#include <cstring>

namespace ThetaGP::Gamepad::Config {
namespace {

// The value standing for a destination that was assigned none: the one value
// outside a row's range that a row carrying kKeyFlagAcceptsUnmapped still
// accepts. It is the sentinel config_defaults.h leaves in the button slots the
// board's key table does not fill.
constexpr int32_t kUnmappedValue =
    static_cast<int32_t>(detail::kBtnMapUnmapped);

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

// The rows tile the store: the widths and element counts of the table add up to
// sizeof(ConfigStore), so every field of the store is carried by a row and no
// byte is carried twice. A field added to the store with no row of its own has
// no name, and a profile body has nothing to write it under, so it breaks this
// build rather than reading back as nothing.
constexpr bool fieldsTileTheStore() {
  size_t bytes = 0;
  for (const KeyEntry &entry : kKeyTable) {
    bytes += static_cast<size_t>(keyTypeWidth(entry.type)) * entry.count;
  }
  // The rows carry every field of the store. The one byte they may leave is the
  // one the store's alignment pads out at its end, which no key could name: a
  // number of bytes short of the whole store by less than that alignment is the
  // store's own tail, and anything else is a byte inside it with no row.
  return bytes <= sizeof(ConfigStore) &&
         sizeof(ConfigStore) - bytes < alignof(ConfigStore);
}

static_assert(fieldsTileTheStore(),
              "key table: the rows do not add up to ConfigStore — a field the "
              "table does not carry has no name and no profile path");

// Every row carries one of the key types, and none of them the sentinel that
// closes the enum. A row of no type is a field no value travels for, and the
// wire has no shape to name it by.
constexpr bool rowsCarryAType() {
  for (const KeyEntry &entry : kKeyTable) {
    if (entry.type == KeyType::Count) {
      return false;
    }
  }
  return true;
}

static_assert(rowsCarryAType(), "key table: a row carries no key type");

// The rows of one domain are adjacent, which is what a profile body walks: the
// writer opens the object a row names, writes the fields of that object and
// closes it when a row of another domain arrives. A domain that came back after
// another would open its object a second time, and the body would carry two
// objects under one name — one of them read by nothing.
constexpr bool sameDomain(const KeyEntry &a, const KeyEntry &b) {
  const char *leafA = profileLeafName(a);
  const char *leafB = profileLeafName(b);
  const size_t lenA = static_cast<size_t>(leafA - a.key);
  const size_t lenB = static_cast<size_t>(leafB - b.key);
  if (lenA != lenB) {
    return false;
  }
  for (size_t i = 0; i < lenA; ++i) {
    if (a.key[i] != b.key[i]) {
      return false;
    }
  }
  return true;
}

constexpr bool domainsAreContiguous() {
  for (uint8_t i = 0; i < kKeyTableCount; ++i) {
    for (uint8_t j = static_cast<uint8_t>(i + 1); j < kKeyTableCount; ++j) {
      if (!sameDomain(kKeyTable[i], kKeyTable[j])) {
        continue;
      }
      for (uint8_t k = static_cast<uint8_t>(i + 1); k < j; ++k) {
        if (!sameDomain(kKeyTable[i], kKeyTable[k])) {
          return false;
        }
      }
    }
  }
  return true;
}

static_assert(domainsAreContiguous(),
              "key table: the rows of one domain are not adjacent — a profile "
              "body would open that object twice");

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

// The exposed rows are the rows the control protocol accepts, and the count of
// them is emitted beside the table by the same declaration that carries the
// flags. The two are held together here, so a row that becomes exposed carries
// the count with it.
constexpr uint8_t exposedRowCount() {
  uint8_t count = 0;
  for (const KeyEntry &entry : kKeyTable) {
    if (isKeyExposed(entry)) {
      ++count;
    }
  }
  return count;
}

static_assert(exposedRowCount() == kExposedKeyCount,
              "key table: the exposed rows and the declared count disagree");

// The key list reply of the control protocol lists the exposed rows, and it is
// sized for kKeyTableMaxEntries of them at kKeyTableMaxNameLen bytes of key
// name each. Both limits are held here, so the reply cannot meet a list it has
// no room for.
constexpr bool listedEntriesWithinLimits() {
  uint8_t listed = 0;
  for (const KeyEntry &entry : kKeyTable) {
    if (!isKeyExposed(entry)) {
      continue;
    }
    ++listed;
    size_t nameLen = 0;
    while (entry.key[nameLen] != '\0') {
      ++nameLen;
    }
    if (nameLen > kKeyTableMaxNameLen) {
      return false;
    }
  }
  return listed <= kKeyTableMaxEntries;
}

static_assert(listedEntriesWithinLimits(),
              "key table: the key list reply is not sized for the rows it lists");

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

const KeyEntry *findExposedKeyEntry(const char *key) {
  const KeyEntry *entry = findKeyEntry(key);
  return (entry != nullptr && isKeyExposed(*entry)) ? entry : nullptr;
}

// Reads element `index` of the row's field out of `cfg` and returns it as a
// signed integer. The element width comes from the row's type, and the bytes are
// copied rather than read through a wider or narrower type. The field's offset
// is added here and nowhere else, so the row addresses the byte the running code
// reads.
int32_t loadKeyElement(const ConfigStore &cfg, const KeyEntry &entry,
                       uint16_t index) {
  const uint8_t *src = reinterpret_cast<const uint8_t *>(&cfg) + entry.offset +
                       static_cast<size_t>(index) * keyTypeWidth(entry.type);
  switch (entry.type) {
  case KeyType::U8:
  case KeyType::U8Array: {
    uint8_t v = 0;
    memcpy(&v, src, sizeof(v));
    return v;
  }
  case KeyType::U16: {
    uint16_t v = 0;
    memcpy(&v, src, sizeof(v));
    return v;
  }
  case KeyType::I16: {
    int16_t v = 0;
    memcpy(&v, src, sizeof(v));
    return v;
  }
  case KeyType::Count:
    break;
  }
  static_assert(static_cast<uint8_t>(KeyType::Count) == 4,
                "key table: a key type added to the enum needs a branch here");
  return 0;
}

// Writes `value` as element `index` of the row's field in `cfg`. The caller has
// already checked the value against what the row accepts. The field's offset is
// added here and nowhere else, so the row addresses the byte the running code
// reads.
void storeKeyElement(ConfigStore &cfg, const KeyEntry &entry, uint16_t index,
                     int32_t value) {
  uint8_t *dst = reinterpret_cast<uint8_t *>(&cfg) + entry.offset +
                 static_cast<size_t>(index) * keyTypeWidth(entry.type);
  switch (entry.type) {
  case KeyType::U8:
  case KeyType::U8Array: {
    const uint8_t v = static_cast<uint8_t>(value);
    memcpy(dst, &v, sizeof(v));
    break;
  }
  case KeyType::U16: {
    const uint16_t v = static_cast<uint16_t>(value);
    memcpy(dst, &v, sizeof(v));
    break;
  }
  case KeyType::I16: {
    const int16_t v = static_cast<int16_t>(value);
    memcpy(dst, &v, sizeof(v));
    break;
  }
  case KeyType::Count:
    break;
  }
  static_assert(static_cast<uint8_t>(KeyType::Count) == 4,
                "key table: a key type added to the enum needs a branch here");
}

// True when `value` is one the row accepts as a single element: inside the
// declared range, or the unmapped sentinel on a row whose flags admit it.
bool keyElementAccepted(const KeyEntry &entry, int32_t value) {
  if (value >= entry.minVal && value <= entry.maxVal) {
    return true;
  }
  return (entry.flags & kKeyFlagAcceptsUnmapped) != 0 &&
         value == kUnmappedValue;
}

} // namespace ThetaGP::Gamepad::Config
