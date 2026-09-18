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

#include "conf/ThetaGP_Config.h"

#include "test/config_cmd_handler.h"
#include "test/dispatcher.h"
#include "test/framelayer.h"

#include "gamepad/config/config_defaults.h"
#include "gamepad/config/config_store.h"
#include "gamepad/config/configmgr.h"
#include "gamepad/config/key_table.h"

#include "utils/log/log.h"

#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace ThetaGP::Test {

using namespace ThetaGP::Gamepad::Config;

using ConfigMgr = ThetaGP::Gamepad::Config::ConfigManager;

// Staging buffer for building response JSON
COMMON_ZERO_INIT static char s_cfgRespBuf[2048];

// The error codes the config domain answers with, as the protocol declares
// them: a key the table does not carry, a value outside what the key accepts
// and a request that names no key at all are all invalid parameters, while a
// command this build cannot carry out names the missing facility instead, and
// one the state of the device refuses names that state.
constexpr int kErrInvalidParam = 2;
constexpr int kErrNotSupported = 6;
constexpr int kErrInvalidState = 8;

// One entry of the key list reply:
//   {"key":"<name>","min":<min>,"max":<max>,"reboot":<bool>}
// with a comma in front of every entry but the first. Its fixed text is the
// literal below, a key name is at most kKeyTableMaxNameLen bytes, and each of
// the two range values is a signed 32-bit decimal, so at most 11 bytes.
constexpr size_t kListKeyEntryMaxBytes =
    sizeof("{\"key\":\"\",\"min\":,\"max\":,\"reboot\":false},") - 1 +
    kKeyTableMaxNameLen + 11 + 11;

// The buffer the entries are built in: a table at the entry limit of the key
// table is listed whole, and a table past it is a build error (the key table
// holds its own count to that limit).
constexpr size_t kListKeysBufSize =
    static_cast<size_t>(kKeyTableMaxEntries) * kListKeyEntryMaxBytes + 1;

// The reply a whole list makes is the entries plus the envelope, the count and
// the brackets around them.
static_assert(sizeof(s_cfgRespBuf) > kListKeysBufSize + 64,
              "config reply buffer: too small for the key list reply");

// The value standing for a destination that was assigned none. It is the one
// value outside minVal..maxVal that a key carrying kKeyFlagAcceptsUnmapped
// still accepts.
constexpr int32_t kUnmappedValue =
    static_cast<int32_t>(detail::kBtnMapUnmapped);

// The lowest signed 32-bit integer. A `value` that is absent, or spelled as
// anything but a plain integer, reads as this, and no key of the table accepts
// a value that low, so a request that arrives with it is refused as carrying no
// accepted integer.
constexpr int kNoIntValue = INT_MIN;

// ── Value access ──

// Reads element `index` of the entry's field out of `store` and returns it as
// a signed integer. The element width comes from the entry's type, and the
// bytes are copied rather than read through a wider or narrower type.
static int32_t loadElement(const uint8_t *store, const KeyEntry &entry,
                           uint16_t index) {
  const uint8_t *src = store + entry.offset +
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
  }
  return 0;
}

// Writes `value` as element `index` of the entry's field in `store`. The
// caller has already checked the value against what the entry accepts.
static void storeElement(uint8_t *store, const KeyEntry &entry, uint16_t index,
                         int32_t value) {
  uint8_t *dst = store + entry.offset +
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
  }
}

// True when `value` is one the entry accepts as a single element: inside the
// declared range, or the unmapped sentinel on a key whose flags admit it.
static bool elementAccepted(const KeyEntry &entry, int32_t value) {
  if (value >= entry.minVal && value <= entry.maxVal) {
    return true;
  }
  return (entry.flags & kKeyFlagAcceptsUnmapped) != 0 &&
         value == kUnmappedValue;
}

// ── Reply helpers ──

// An error reply carries the three keys the protocol gives that shape: the
// outcome, the code and what was wrong with the request.
static void sendError(int errorCode, const char *reason) {
  Json resp;
  resp.beginWrite(s_cfgRespBuf, sizeof(s_cfgRespBuf));
  resp.printf("{status:%Q,error_code:%d,reason:%Q}", "error", errorCode,
              reason);
  uint16_t len = resp.end();
  FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

// The field of the configuration in effect that the entry stands for.
static uint8_t *fieldOf(const KeyEntry &entry) {
  return reinterpret_cast<uint8_t *>(&ConfigMgr::getInstance().configMut()) +
         entry.offset;
}

// The entry the request's `key` field names, null-terminated into `name` so
// the reply can echo it. A field longer than `name` is cut to fit the buffer
// rather than refused, and what is left is not the name the caller wrote, so
// the lookup below turns it away. Returns nullptr when the field is missing or
// names no key of the table.
static const KeyEntry *requestedKey(const Json &json, char *name, size_t cap) {
  if (!json.getStrCopy("key", name, static_cast<int>(cap))) {
    return nullptr;
  }
  return findKeyEntry(name);
}

// ── config.set_key ──

static void handleConfigSetKey(const char *cmd, const Json &json) {
  const int q = json.getInt("queued");

  char keyName[64];
  const KeyEntry *entry = requestedKey(json, keyName, sizeof(keyName));
  if (!entry) {
    sendError(kErrInvalidParam, "missing or unknown key");
    return;
  }

  uint8_t *field = fieldOf(*entry);
  if (entry->type == KeyType::U8Array) {
    // An array value is counted and checked element by element before the
    // first byte is written, so a value with one bad element leaves the field
    // exactly as it was.
    const int count = json.getArrLen("value");
    if (count != static_cast<int>(entry->count)) {
      char reason[64];
      snprintf(reason, sizeof(reason), "value must hold %u elements",
               static_cast<unsigned>(entry->count));
      sendError(kErrInvalidParam, reason);
      return;
    }

    for (uint16_t i = 0; i < entry->count; ++i) {
      const int32_t value = json.getArrInt("value", i, kNoIntValue);
      if (value == kNoIntValue || !elementAccepted(*entry, value)) {
        char reason[64];
        snprintf(reason, sizeof(reason), "value[%u] is not accepted",
                 static_cast<unsigned>(i));
        sendError(kErrInvalidParam, reason);
        return;
      }
    }

    for (uint16_t i = 0; i < entry->count; ++i) {
      storeElement(
          field, *entry, i,
          static_cast<int32_t>(json.getArrInt("value", i, kNoIntValue)));
    }
  } else {
    const int32_t value =
        static_cast<int32_t>(json.getInt("value", kNoIntValue));
    if (value == kNoIntValue) {
      // A value of exactly this number reaches here as well: it is the one
      // integer the sentinel stands for, and no key accepts it. Whether the
      // field was there tells the two apart, so a number that was sent is not
      // answered with a claim that nothing arrived.
      sendError(kErrInvalidParam,
                json.has("value") ? "value is not an integer this key accepts"
                                  : "missing value");
      return;
    }
    if (!elementAccepted(*entry, value)) {
      char reason[64];
      snprintf(reason, sizeof(reason), "value is outside %ld..%ld",
               static_cast<long>(entry->minVal),
               static_cast<long>(entry->maxVal));
      sendError(kErrInvalidParam, reason);
      return;
    }
    storeElement(field, *entry, 0, value);
  }

  LOG_INFO("ConfigCmdHandler: set %s", entry->key);

  Json resp;
  resp.beginWrite(s_cfgRespBuf, sizeof(s_cfgRespBuf));
  resp.printf("{cmd:%Q,queued:%d,status:%Q,key:%Q}", cmd, q + 1, "ok",
              entry->key);
  uint16_t len = resp.end();
  FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

// ── config.get_key ──

static void handleConfigGetKey(const char *cmd, const Json &json) {
  const int q = json.getInt("queued");

  char keyName[64];
  const KeyEntry *entry = requestedKey(json, keyName, sizeof(keyName));
  if (!entry) {
    sendError(kErrInvalidParam, "missing or unknown key");
    return;
  }

  const uint8_t *field =
      reinterpret_cast<const uint8_t *>(&ConfigMgr::getInstance().config()) +
      entry->offset;

  Json resp;
  resp.beginWrite(s_cfgRespBuf, sizeof(s_cfgRespBuf));

  if (entry->type == KeyType::U8Array) {
    // The run of elements is rendered as text and handed over whole: the write
    // side has no specifier that walks a run of integers. An element of an
    // array key is one byte of the store, so it reads as at most 3 characters
    // beside its separator, and this buffer holds 96 of them — past the longest
    // run the table carries today. A run that still overflows it is refused
    // below rather than cut short and sent as a value.
    char elems[32 * 12 + 1];
    size_t used = 0;
    bool truncated = false;
    for (uint16_t i = 0; i < entry->count; ++i) {
      const int n = snprintf(elems + used, sizeof(elems) - used,
                             (i == 0) ? "%ld" : ",%ld",
                             static_cast<long>(loadElement(field, *entry, i)));
      if (n < 0) {
        break;
      }
      used += static_cast<size_t>(n);
      if (used >= sizeof(elems)) {
        truncated = true;
        used = sizeof(elems) - 1;
        break;
      }
    }

    if (truncated) {
      // A run cut short is not the value the key holds, so it is refused
      // instead of being sent as one.
      LOG_ERROR("ConfigCmdHandler: value of %s does not fit %u bytes",
                entry->key, static_cast<unsigned>(sizeof(elems)));
      sendError(kErrNotSupported, "value does not fit the reply buffer");
      return;
    }

    elems[used] = '\0';
    resp.printf("{cmd:%Q,queued:%d,status:%Q,key:%Q,value:[%s]}", cmd, q + 1,
                "ok", entry->key, elems);
  } else {
    resp.printf("{cmd:%Q,queued:%d,status:%Q,key:%Q,value:%ld}", cmd, q + 1,
                "ok", entry->key,
                static_cast<long>(loadElement(field, *entry, 0)));
  }

  uint16_t len = resp.end();
  FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

// ── config.list_keys ──
// One object per key of the table, so the answer to "which keys does this
// firmware carry" is the table itself and not a list written out here.

static void handleConfigListKeys(const char *cmd, const Json &json) {
  const int q = json.getInt("queued");

  // Sized for a table at the entry limit of the key table, so the list of a
  // table within that limit is always whole (the limit is held in
  // key_table.cpp).
  char entries[kListKeysBufSize];
  size_t used = 0;
  bool truncated = false;
  const KeyEntry *table = keyTable();
  for (uint8_t i = 0; i < keyTableCount(); ++i) {
    const KeyEntry &entry = table[i];
    const int n = snprintf(
        entries + used, sizeof(entries) - used,
        "%s{\"key\":\"%s\",\"min\":%ld,\"max\":%ld,\"reboot\":%s}",
        (i == 0) ? "" : ",", entry.key, static_cast<long>(entry.minVal),
        static_cast<long>(entry.maxVal),
        (entry.flags & kKeyFlagRequiresReboot) ? "true" : "false");
    if (n < 0) {
      break;
    }
    used += static_cast<size_t>(n);
    if (used >= sizeof(entries) - 1) {
      truncated = true;
      used = sizeof(entries) - 1;
      break;
    }
  }
  entries[used] = '\0';

  if (truncated) {
    // A reply cut short is not a JSON document, so it is refused instead of
    // being sent as one.
    LOG_ERROR("ConfigCmdHandler: key list does not fit %u bytes",
              static_cast<unsigned>(sizeof(entries)));
    sendError(kErrNotSupported, "key list does not fit the reply buffer");
    return;
  }

  Json resp;
  resp.beginWrite(s_cfgRespBuf, sizeof(s_cfgRespBuf));
  resp.printf("{cmd:%Q,queued:%d,status:%Q,count:%u,keys:[%s]}", cmd, q + 1,
              "ok", static_cast<unsigned>(keyTableCount()), entries);
  uint16_t len = resp.end();
  FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

// ── config.save ──
// Writes the configuration in effect to the profile it belongs to. A board
// without a storage chip keeps the configuration in RAM, and answers persisted
// false: the values are in effect, they just do not outlive the power cycle,
// and that is a fact of the reply rather than a failure of the command. A board
// that has storage writes and says so, or refuses and names what stopped it, so
// that persisted true is only ever the answer to a write that reached the
// flash.

static void handleConfigSave(const char *cmd, const Json &json) {
  const int q = json.getInt("queued");

  bool persisted = false;

#if THETAGP_CFG_HAS_FLASH
  // The factory profile is the board's baseline and the configuration layer
  // refuses to write it: the reply names that state instead of reporting a
  // write that was never attempted.
  if (ConfigMgr::getInstance().activeProfileId() == 0) {
    sendError(kErrInvalidState, "the active profile is the factory one");
    return;
  }
  if (!ConfigMgr::getInstance().saveProfile()) {
    sendError(kErrInvalidState, "the active profile could not be written");
    return;
  }
  persisted = true;
#else
  // Nothing to write to: the values stay in effect for this session only.
#endif

  Json resp;
  resp.beginWrite(s_cfgRespBuf, sizeof(s_cfgRespBuf));
  resp.printf("{cmd:%Q,queued:%d,status:%Q,persisted:%B}", cmd, q + 1, "ok",
              persisted ? 1 : 0);
  uint16_t len = resp.end();
  FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

// ── config.load ──
// Reads the profile the configuration in effect belongs to back over it.

static void handleConfigLoad(const char *cmd, const Json &json) {
  const int q = json.getInt("queued");

#if THETAGP_CFG_HAS_FLASH
  if (!ConfigMgr::getInstance().loadProfile(
          ConfigMgr::getInstance().activeProfileId())) {
    sendError(kErrInvalidParam, "no readable profile on the active slot");
    return;
  }
#else
  // Nothing to read from: the command names a facility this build does not
  // carry rather than reporting a read that failed. The refusal keeps the
  // error reply's shape, which names no command.
  (void)cmd;
  sendError(kErrNotSupported, "no persistent storage on this board");
  return;
#endif

  Json resp;
  resp.beginWrite(s_cfgRespBuf, sizeof(s_cfgRespBuf));
  resp.printf("{cmd:%Q,queued:%d,status:%Q}", cmd, q + 1, "ok");
  uint16_t len = resp.end();
  FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

// ── config.factory_reset ──
// Replaces the configuration in effect with the compiled-in defaults. Writing
// it out is config.save's job, so nothing reaches persistent storage here and
// the reply reports that.

static void handleConfigFactoryReset(const char *cmd, const Json &json) {
  const int q = json.getInt("queued");

  ConfigMgr::getInstance().configMut() = kConfigDefaults;

  Json resp;
  resp.beginWrite(s_cfgRespBuf, sizeof(s_cfgRespBuf));
  resp.printf("{cmd:%Q,queued:%d,status:%Q,persisted:%B}", cmd, q + 1, "ok", 0);
  uint16_t len = resp.end();
  FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

// ── Domain Dispatch ──

void ConfigCmdHandler::handleConfig(const char *cmd, const Json &json) {
  LOG_DEBUG("ConfigCmdHandler: cmd='%s'", cmd);

  if (strcmp(cmd, "config.set_key") == 0) {
    handleConfigSetKey(cmd, json);
  } else if (strcmp(cmd, "config.get_key") == 0) {
    handleConfigGetKey(cmd, json);
  } else if (strcmp(cmd, "config.list_keys") == 0) {
    handleConfigListKeys(cmd, json);
  } else if (strcmp(cmd, "config.save") == 0) {
    handleConfigSave(cmd, json);
  } else if (strcmp(cmd, "config.load") == 0) {
    handleConfigLoad(cmd, json);
  } else if (strcmp(cmd, "config.factory_reset") == 0) {
    handleConfigFactoryReset(cmd, json);
  } else {
    LOG_WARN("ConfigCmdHandler: unknown config command '%s'", cmd);
    sendError(kErrNotSupported, "unknown config command");
  }
}

// ── Registration ──

ConfigCmdHandler &ConfigCmdHandler::getInstance() {
  static ConfigCmdHandler instance;
  return instance;
}

void ConfigCmdHandler::registerHandlers() {
  Dispatcher::getInstance().registerHandler("config", handleConfig);
  LOG_INFO("ConfigCmdHandler registered: config. domain");
}

} // namespace ThetaGP::Test
