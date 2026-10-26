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

#include "gamepad/config/config_manager.h"
#include "conf/ThetaGP_Config.h" // THETAGP_CFG_HAS_FLASH, the flash switch this file branches on
#include "gamepad/config/config_defaults.h"
#include "gamepad/config/config_store.h"
#include "gamepad/profile/profile_store.h"
#include "utils/json/json.h" // Util::Json::missingKeyCount, the key comparison

#include "utils/log/log.h"

#include <cstring> // memcpy, copying the body being replaced out of the store

namespace ThetaGP::Gamepad::Config {

ConfigManager &ConfigManager::getInstance() {
  static ConfigManager instance;
  return instance;
}

// The profile the configuration the device runs on belongs to: the store's
// active profile. The layer keeps no reading of its own, and getStatus() is
// where each build says where that record comes from.
uint16_t ConfigManager::activeProfileId() const { return getStatus().activeId; }

// Reserved: not implemented, so it moves nothing.
void ConfigManager::setActiveProfileId(uint16_t) {}

#if THETAGP_CFG_HAS_FLASH

using Profile::PROFILE_ID_ACTIVE;
using Profile::PROFILE_STAGING_SIZE;
using Profile::ProfileStore;
using Profile::ProfileText;
using Profile::s_staging;

bool ConfigManager::init() {
  // The profile below overrides only the fields it carries; every other field
  // stays at the compiled-in default.
  _config = kConfigDefaults;

  ProfileStore &store = ProfileStore::getInstance();
  if (!store.init()) {
    LOG_ERROR("ConfigManager: ProfileStore init failed");
    return false;
  }

  // A flash that carries no profile gets the factory one, whose text is this
  // configuration (the compiled defaults).
  (void)ensureFactoryProfile();

  Profile::ProfileStatus status = store.getStatus();

  ProfileText text;
  if (!store.readProfile(PROFILE_ID_ACTIVE, &text)) {
    LOG_WARN("ConfigManager: no readable profile, using defaults");
    return false;
  }

  // The body is parsed over the compiled defaults, in the window readProfile
  // reported. A refused body leaves those defaults in place.
  if (text.len > 0 && !parseProfile(text.data, text.len, &_config)) {
    LOG_WARN("ConfigManager: the profile body was refused, the configuration "
             "stays at the compiled defaults");
  }

  // The factory body is kept in this firmware's format, and rewritten from the
  // compiled defaults when the flash carries one of an older format.
  (void)repairFactoryProfile();

  applySys();

  LOG_INFO("ConfigManager: init OK, active=%u count=%u", status.activeId,
           status.profileCount);
  return true;
}

// Writes the factory Profile 0 on a flash that carries none, and resets the
// configuration to what that body holds.
bool ConfigManager::ensureFactoryProfile() {
  ProfileStore &store = ProfileStore::getInstance();
  if (!store.needsFactoryProfile()) {
    return true; // the flash carries a profile; RAM is the caller's business
  }

  // The body is the compiled defaults, not _config: _config can still describe
  // a profile that an erase just removed, and Profile0 is by definition the
  // compiled-in baseline. cap is the size of the buffer, not a body limit.
  const uint16_t jsonLen = serializeProfile(
      kConfigDefaults, "", reinterpret_cast<char *>(s_staging),
      static_cast<uint16_t>(PROFILE_STAGING_SIZE));
  if (jsonLen == 0) {
    LOG_ERROR("ConfigManager: factory Profile0 body does not fit the staging "
              "buffer");
    return false;
  }

  if (!store.writeFactoryProfile(reinterpret_cast<const char *>(s_staging),
                                 jsonLen)) {
    LOG_WARN("ConfigManager: factory Profile0 write failed");
    return false;
  }

  // The flash now holds exactly the compiled defaults, so the RAM copy of the
  // configuration becomes those defaults too.
  _config = kConfigDefaults;

  LOG_INFO("ConfigManager: factory Profile0 written, %u bytes", jsonLen);
  return true;
}

// The factory body is the compiled defaults, so a body of an older format is
// written again from them.
bool ConfigManager::repairFactoryProfile() {
  ProfileStore &store = ProfileStore::getInstance();

  ProfileText held;
  bool reads = store.readProfile(0, &held) && held.len > 0;
  if (reads) {
    Util::Json body;
    body.parse(held.data, static_cast<int>(held.len));
    if (body.getInt("ver", kOldestReadableVersion) == kProfileVersion) {
      return true;
    }
  }

  const uint16_t jsonLen = serializeProfile(
      kConfigDefaults, "", reinterpret_cast<char *>(s_staging),
      static_cast<uint16_t>(PROFILE_STAGING_SIZE));
  if (jsonLen == 0) {
    LOG_ERROR("ConfigManager: the factory body does not fit the staging buffer");
    return false;
  }
  if (!store.replaceFactoryProfile(reinterpret_cast<const char *>(s_staging),
                                   jsonLen)) {
    LOG_WARN("ConfigManager: the factory body was not rewritten");
    return false;
  }
  LOG_INFO("ConfigManager: factory body rewritten, %u bytes", jsonLen);
  return true;
}

// Reserved: not implemented, so it reads nothing and answers that it did not.
bool ConfigManager::loadProfile(uint16_t) { return false; }

bool ConfigManager::readProfileBody(uint16_t profileId) {
  // The configuration is reset to the compiled defaults before the read, so a
  // body of length 0 leaves it at those defaults.
  _config = kConfigDefaults;

  // The id read is the one this call was handed, whether or not the store calls
  // that profile active: the active profile and the profile being read are one
  // reading only for the entry that moved onto it first.
  ProfileText text;
  bool ok = ProfileStore::getInstance().readProfile(profileId, &text);
  // text.len is the parse window. A refused body leaves the configuration at
  // the defaults the reset applied, and the read answers that it failed.
  if (ok && text.len > 0 && !parseProfile(text.data, text.len, &_config)) {
    LOG_WARN("ConfigManager: id=%u carries a body this firmware does not read",
             profileId);
    ok = false;
  }
  applySys();
  LOG_INFO("ConfigManager: load id=%u %s", profileId, ok ? "OK" : "FAIL");
  return ok;
}

// The compiled defaults are the reset's whole effect: nothing is read from the
// flash and nothing is written to it, so the body the device persists is left
// where it was and a save is what puts these values there.
void ConfigManager::factoryReset() {
  _config = kConfigDefaults;
  applySys();
}

void ConfigManager::applySys() {
#if THETAGP_CFG_LOG_EN
  // The threshold a profile names, as the logger holds it: the debug lines a
  // build keeps are its own switch, not a level a profile picks.
  switch (_config.sys.log) {
  case 0:
    LogSetLevel(LOG_LV(Info));
    break;
  case 1:
    LogSetLevel(LOG_LV(Warn));
    break;
  default:
    LogSetLevel(LOG_LV(Error));
    break;
  }
#endif
}

// ── saveProfile() ──
// The body this save replaces is read back before the write, into a buffer of its
// own: the save reports the keys it did not carry over.
static COMMON_ZERO_INIT uint8_t s_replaced[PROFILE_STAGING_SIZE];

bool ConfigManager::saveProfile(uint32_t *droppedKeys) {
  ProfileStore &store = ProfileStore::getInstance();

  // The profile this save writes to, read from the store once: it is the one id
  // the whole save stands on.
  const uint16_t activeId = store.getStatus().activeId;

  // What the caller is handed is raised by the write below and by nothing
  // else, so every path that returns before it leaves this at 0.
  if (droppedKeys) {
    *droppedKeys = 0;
  }

  if (activeId == 0) {
    LOG_WARN("ConfigManager: cannot save to factory Profile0");
    return false;
  }

  // How many keys of the replaced body the new one does not carry, counted against
  // the body about to be written. The replaced body is read either way: its name
  // is what the new one carries on.
  bool haveReplaced = false;
  uint16_t replacedLen = 0;
  {
    ProfileText replaced;
    if (store.readProfile(activeId, &replaced) && replaced.len > 0) {
      // ProfileStore::readBody reports at most PROFILE_JSON_MAX bytes, one below
      // this buffer.
      replacedLen = replaced.len;
      memcpy(s_replaced, replaced.data, replacedLen);
      haveReplaced = true;
    } else if (droppedKeys) {
      // Nothing to compare against. The write below still happens; what it does
      // not carry over stays unreported rather than reported as none.
      LOG_WARN("ConfigManager: the body being replaced could not be read; the "
               "save cannot say what it does not carry over");
    }
  }

  char carriedName[kProfileNameMax] = {};
  if (haveReplaced) {
    Util::Json replaced;
    replaced.parse(reinterpret_cast<const char *>(s_replaced), replacedLen);
    (void)replaced.getStrCopy("name", carriedName, sizeof(carriedName));
  }

  // The whole staging buffer is offered, so the largest body the flash layer
  // accepts (PROFILE_JSON_MAX) still fits with its terminator. A length of 0
  // means the serializer produced no usable body — there is nothing to persist.
  const uint16_t jsonLen = serializeProfile(
      _config, carriedName, reinterpret_cast<char *>(s_staging),
      static_cast<uint16_t>(PROFILE_STAGING_SIZE));
  if (jsonLen == 0) {
    LOG_ERROR("ConfigManager: nothing to save, serialization produced no body");
    return false;
  }

  uint32_t notCarriedOver = 0;
  if (droppedKeys && haveReplaced) {
    Util::Json fresh;
    fresh.parse(reinterpret_cast<const char *>(s_staging), jsonLen);
    Util::Json replaced;
    replaced.parse(reinterpret_cast<const char *>(s_replaced), replacedLen);
    notCarriedOver = fresh.missingKeyCount(replaced);
  }

  if (!store.modifyProfile(activeId, reinterpret_cast<const char *>(s_staging),
                           jsonLen)) {
    LOG_ERROR("ConfigManager: save failed id=%u", activeId);
    return false;
  }

  // Handed over only after the write reached the flash: the count is a fact of
  // the save that happened, not of the one that was prepared.
  if (droppedKeys) {
    *droppedKeys = notCarriedOver;
  }

  LOG_INFO("ConfigManager: save OK id=%u, len=%u, dropped=%u", activeId,
           jsonLen, static_cast<unsigned>(notCarriedOver));
  return true;
}

uint8_t ConfigManager::profileCount() const {
  return ProfileStore::getInstance().getStatus().profileCount;
}

// The profile the device runs from is the store's own entry, and its body is
// what the device runs on: the configuration in effect is read from it, so a
// switch takes effect without a reboot.
bool ConfigManager::selectProfile(uint16_t id) {
  if (!ProfileStore::getInstance().selectProfile(id)) {
    return false;
  }
  return readProfileBody(id);
}

Profile::ProfileStatus ConfigManager::getStatus() const {
  return ProfileStore::getInstance().getStatus();
}

#else // THETAGP_CFG_HAS_FLASH

// Without a flash chip there is nowhere to keep profiles: the configuration runs
// on the compiled defaults, and nothing survives a power cycle.

bool ConfigManager::init() {
  _config = kConfigDefaults;
  applySys();
  LOG_INFO("ConfigManager: no flash chip, running on defaults");
  return true;
}

bool ConfigManager::loadProfile(uint16_t) { return false; }

// No storage to read from, so there is no body to read and no profile to move
// onto: the configuration stays at the compiled defaults it was initialized
// with.
bool ConfigManager::readProfileBody(uint16_t) { return false; }

// No storage to write to either, which is what the reset does anyway: the
// compiled defaults replace the configuration in effect and nothing else moves.
void ConfigManager::factoryReset() {
  _config = kConfigDefaults;
  applySys();
}

void ConfigManager::applySys() {
#if THETAGP_CFG_LOG_EN
  // The three levels a profile chooses among: a debug line is the build's
  // switch (THETAGP_CFG_LOG_DEBUG), not a threshold a body carries.
  switch (_config.sys.log) {
  case 0:
    LogSetLevel(LOG_LV(Info));
    break;
  case 1:
    LogSetLevel(LOG_LV(Warn));
    break;
  default:
    LogSetLevel(LOG_LV(Error));
    break;
  }
#endif
}

bool ConfigManager::saveProfile(uint32_t *droppedKeys) {
  // No storage to write to, so no body is replaced and nothing can be left
  // behind: the call writes nothing, which leaves the caller's count at 0 —
  // the same answer every path that returns without a write gives.
  if (droppedKeys) {
    *droppedKeys = 0;
  }
  return false;
}

uint8_t ConfigManager::profileCount() const { return 1; }

bool ConfigManager::selectProfile(uint16_t) { return false; }

Profile::ProfileStatus ConfigManager::getStatus() const { return {}; }

#endif // THETAGP_CFG_HAS_FLASH

} // namespace ThetaGP::Gamepad::Config
