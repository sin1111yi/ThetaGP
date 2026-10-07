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

#include "wire/config_d.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "gamepad/config/config_manager.h"
#include "gamepad/config/key_table.h"
#include "wire/dispatch.h"

namespace ThetaGP::Wire {

using Gamepad::Config::ConfigManager;
using Gamepad::Config::ConfigStore;
using Gamepad::Config::findExposedKeyEntry;
using Gamepad::Config::isKeyExposed;
using Gamepad::Config::keyElementAccepted;
using Gamepad::Config::KeyEntry;
using Gamepad::Config::kKeyFlagRequiresReboot;
using Gamepad::Config::keyTable;
using Gamepad::Config::keyTableCount;
using Gamepad::Config::KeyType;
using Gamepad::Config::loadKeyElement;
using Gamepad::Config::storeKeyElement;

namespace {

// The key list reply is built from the table's exposed rows, and the schema
// sizes its buffers with the two limits the table declares for them. The pair
// is held together here, so a table that outgrows the reply is a build error
// rather than a list cut short at run time.
static_assert(Gamepad::Config::kKeyTableMaxEntries <=
                  sizeof(((ThetaGP_ConfigListKeysOk *)nullptr)->keys) /
                      sizeof(ThetaGP_ConfigKeyEntry),
              "config list: the key table holds more keys than the list reply");
static_assert(Gamepad::Config::kKeyTableMaxNameLen <
                  sizeof(((ThetaGP_ConfigKeyEntry *)nullptr)->key),
              "config list: a key name outgrows the reply's own buffer");

// The key name an accepted arm answers with: the name the row spells, which is
// the name the request named.
void writeKey(char *out, size_t cap, const KeyEntry &entry) {
  std::snprintf(out, cap, "%s", entry.key);
}

// The row the request names, or the refusal a caller is owed for a key this
// firmware does not carry. A request whose name field is empty names no key at
// all, which is told apart from a name no exposed row carries: the first is a
// value that arrived missing, the second a field the control protocol does not
// reach.
const KeyEntry *requestedKey(const char *name, ThetaGP_Reply &reply) {
  if (name[0] == '\0') {
    writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                 ThetaGP_Reason_REASON_VALUE_MISSING);
    return nullptr;
  }
  const KeyEntry *entry = findExposedKeyEntry(name);
  if (entry == nullptr) {
    writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                 ThetaGP_Reason_REASON_UNKNOWN_KEY);
  }
  return entry;
}

} // namespace

void ConfigDomain::getKey(const ThetaGP_Request &request, ThetaGP_Reply &reply) {
  const KeyEntry *entry = requestedKey(request.kind.config_get_key.key, reply);
  if (entry == nullptr) {
    return;
  }

  const ConfigStore &store = ConfigManager::getInstance().config();
  reply.which_kind = ThetaGP_Reply_config_get_key_tag;
  ThetaGP_ConfigGetKeyOk &ok = reply.kind.config_get_key;

  if (entry->type == KeyType::U8Array) {
    // The run the key holds, read element by element at the row's offset. The
    // reply carries as many elements as the row declares and says so, so the
    // count and the elements never disagree.
    ok.which_value = ThetaGP_ConfigGetKeyOk_array_tag;
    ok.value.array.elements_count = entry->count;
    for (uint16_t i = 0; i < entry->count; ++i) {
      ok.value.array.elements[i] = loadKeyElement(store, *entry, i);
    }
  } else {
    ok.which_value = ThetaGP_ConfigGetKeyOk_scalar_tag;
    ok.value.scalar = loadKeyElement(store, *entry, 0);
  }

  writeKey(ok.key, sizeof ok.key, *entry);
}

void ConfigDomain::setKey(const ThetaGP_Request &request, ThetaGP_Reply &reply) {
  const KeyEntry *entry = requestedKey(request.kind.config_set_key.key, reply);
  if (entry == nullptr) {
    return;
  }

  const ThetaGP_ConfigSetKey &requested = request.kind.config_set_key;
  ConfigStore &store = ConfigManager::getInstance().configMut();

  if (entry->type == KeyType::U8Array) {
    if (requested.which_value != ThetaGP_ConfigSetKey_array_tag ||
        requested.value.array.elements_count != entry->count) {
      // The value is not a run of the length the key holds: the refusal names
      // that length, which is the count the row declares.
      writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                   ThetaGP_Reason_REASON_VALUE_ELEMENT_COUNT, entry->count);
      return;
    }
    // Every element is checked before the first byte is written, so a run with
    // one bad element leaves the field exactly as it was.
    for (uint16_t i = 0; i < entry->count; ++i) {
      if (!keyElementAccepted(*entry, requested.value.array.elements[i])) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_VALUE_ELEMENT_REJECTED, i);
        return;
      }
    }
    for (uint16_t i = 0; i < entry->count; ++i) {
      storeKeyElement(store, *entry, i, requested.value.array.elements[i]);
    }
  } else {
    if (requested.which_value != ThetaGP_ConfigSetKey_scalar_tag) {
      // A scalar key takes one number, and the request carried none.
      writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                   ThetaGP_Reason_REASON_VALUE_MISSING);
      return;
    }
    // The number travels as a 64-bit field and the field it reaches is at most
    // 16 bits wide, so a value no element of that width could hold is outside
    // the range the row declares by construction.
    const int64_t value = requested.value.scalar;
    if (value < INT32_MIN || value > INT32_MAX ||
        !keyElementAccepted(*entry, static_cast<int32_t>(value))) {
      writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                   ThetaGP_Reason_REASON_VALUE_OUT_OF_RANGE,
                   static_cast<uint32_t>(entry->minVal),
                   static_cast<uint32_t>(entry->maxVal));
      return;
    }
    storeKeyElement(store, *entry, 0, static_cast<int32_t>(value));
  }

  reply.which_kind = ThetaGP_Reply_config_set_key_tag;
  writeKey(reply.kind.config_set_key.key, sizeof reply.kind.config_set_key.key,
           *entry);
}

void ConfigDomain::listKeys(ThetaGP_Reply &reply) {
  reply.which_kind = ThetaGP_Reply_config_list_keys_tag;
  ThetaGP_ConfigListKeysOk &ok = reply.kind.config_list_keys;

  // The reply is the table itself and not a list written out here: every
  // exposed row is listed, in the table's own order. The rows a profile carries
  // and no command reaches are left out, because a caller cannot name them.
  uint32_t listed = 0;
  for (uint8_t i = 0; i < keyTableCount(); ++i) {
    const KeyEntry &entry = keyTable()[i];
    if (!isKeyExposed(entry)) {
      continue;
    }
    ThetaGP_ConfigKeyEntry &row = ok.keys[listed];
    writeKey(row.key, sizeof row.key, entry);
    row.min = entry.minVal;
    row.max = entry.maxVal;
    row.reboot = (entry.flags & kKeyFlagRequiresReboot) != 0;
    ++listed;
  }
  ok.keys_count = static_cast<pb_size_t>(listed);
  ok.count = listed;
}

} // namespace ThetaGP::Wire
