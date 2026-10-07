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

#include <cstdio>
#include <cstring>

#include "configs/config_keys.gen.h"
#include "gamepad/config/config_manager.h"
#include "gamepad/config/key_table.h"
#include "wire/dispatch.h"

namespace ThetaGP::Wire {

using Gamepad::Config::ConfigManager;
using Gamepad::Config::ConfigStore;
using Gamepad::Config::findExposedKeyEntry;
using Gamepad::Config::KeyEntry;
using Gamepad::Config::KeyType;

namespace {

// A field of the store as the bytes the table's offset names.
const uint8_t *fieldAt(const ConfigStore &store, const KeyEntry &entry) {
  return reinterpret_cast<const uint8_t *>(&store) + entry.offset;
}

uint8_t *fieldAt(ConfigStore &store, const KeyEntry &entry) {
  return reinterpret_cast<uint8_t *>(&store) + entry.offset;
}

// A scalar field's value, widened. The run a U8Array holds is not one number,
// so it reads as none.
int64_t readScalar(const ConfigStore &store, const KeyEntry &entry) {
  const uint8_t *at = fieldAt(store, entry);
  uint16_t wide = 0;
  switch (entry.type) {
  case KeyType::U8:
    return *at;
  case KeyType::U16:
  case KeyType::I16:
    std::memcpy(&wide, at, sizeof wide);
    return entry.type == KeyType::U16 ? static_cast<int64_t>(wide)
                                      : static_cast<int64_t>(static_cast<int16_t>(wide));
  default:
    return 0;
  }
}

// Writes a scalar field, narrowed to the width the key declares.
void writeScalar(ConfigStore &store, const KeyEntry &entry, int64_t value) {
  uint8_t *at = fieldAt(store, entry);
  switch (entry.type) {
  case KeyType::U8: {
    const uint8_t held = static_cast<uint8_t>(value);
    std::memcpy(at, &held, sizeof held);
    break;
  }
  case KeyType::U16: {
    const uint16_t held = static_cast<uint16_t>(value);
    std::memcpy(at, &held, sizeof held);
    break;
  }
  case KeyType::I16: {
    const int16_t held = static_cast<int16_t>(value);
    std::memcpy(at, &held, sizeof held);
    break;
  }
  default:
    break;
  }
}

// The key the request names, or the refusal a caller is owed for one this
// firmware does not carry. A key the table holds but does not expose is not
// one the control protocol reaches, and reads as unknown here.
const KeyEntry *exposedKey(const char *name, ThetaGP_Reply &reply) {
  const KeyEntry *entry = findExposedKeyEntry(name);
  if (entry == nullptr) {
    writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                 ThetaGP_Reason_REASON_NONE);
  }
  return entry;
}

} // namespace

void ConfigDomain::getKey(const ThetaGP_Request &request, ThetaGP_Reply &reply) {
  const KeyEntry *entry =
      exposedKey(request.kind.config_get_key.key, reply);
  if (entry == nullptr) {
    return;
  }
  if (entry->type == KeyType::U8Array) {
    writeFailure(reply, ThetaGP_ErrorCode_ERR_NOT_SUPPORTED,
                 ThetaGP_Reason_REASON_NOT_IMPLEMENTED);
    return;
  }

  const ConfigStore &store = ConfigManager::getInstance().config();
  reply.which_kind = ThetaGP_Reply_config_get_key_tag;
  ThetaGP_ConfigGetKeyOk &ok = reply.kind.config_get_key;
  ok.which_value = ThetaGP_ConfigGetKeyOk_scalar_tag;
  ok.value.scalar = readScalar(store, *entry);
  std::snprintf(ok.key, sizeof ok.key, "%s", entry->key);
}

void ConfigDomain::setKey(const ThetaGP_Request &request, ThetaGP_Reply &reply) {
  const KeyEntry *entry =
      exposedKey(request.kind.config_set_key.key, reply);
  if (entry == nullptr) {
    return;
  }
  if (entry->type == KeyType::U8Array) {
    writeFailure(reply, ThetaGP_ErrorCode_ERR_NOT_SUPPORTED,
                 ThetaGP_Reason_REASON_NOT_IMPLEMENTED);
    return;
  }

  const ThetaGP_ConfigSetKey &requested = request.kind.config_set_key;
  const int64_t value = requested.value.scalar;
  if (value < entry->minVal || value > entry->maxVal) {
    writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                 ThetaGP_Reason_REASON_NONE);
    return;
  }

  writeScalar(ConfigManager::getInstance().configMut(), *entry, value);

  reply.which_kind = ThetaGP_Reply_config_set_key_tag;
  std::snprintf(reply.kind.config_set_key.key,
                sizeof reply.kind.config_set_key.key, "%s", entry->key);
}

} // namespace ThetaGP::Wire
