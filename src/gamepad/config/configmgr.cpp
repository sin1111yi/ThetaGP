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

#include "gamepad/config/configmgr.h"
#include "conf/ThetaGP_Config.h" // THETAGP_CFG_HAS_FLASH, the flash switch this file branches on
#include "gamepad/config/config_defaults.h"
#include "gamepad/config/config_store.h"
#include "gamepad/profile/profile_store.h"

#include "utils/log/log.h"

namespace ThetaGP::Gamepad::Config {

ConfigManager &ConfigManager::getInstance() {
  static ConfigManager instance;
  return instance;
}

uint16_t ConfigManager::activeProfileId() const { return _activeId; }

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
  _activeId = status.activeId;

  ProfileText text;
  if (!store.readProfile(PROFILE_ID_ACTIVE, &text)) {
    LOG_WARN("ConfigManager: no readable profile, using defaults");
    return false;
  }

  // Parse the profile body into _config, on top of the compiled-in defaults.
  if (text.len > 0) {
    parseProfile(text.data, &_config);
  }

  LOG_INFO("ConfigManager: init OK, active=%u count=%u", _activeId,
           status.profileCount);
  return true;
}

// Restore the factory Profile0 on a flash that carries no profile, and reset
// the active configuration to what that body holds. Both callers go through
// here: init() on a fresh flash, and test.chip_erase, which wipes the chip
// without a reboot — the question "is there a profile?" is put to the flash on
// the spot (ProfileStore::needsFactoryProfile()) instead of being answered once
// at boot.
bool ConfigManager::ensureFactoryProfile() {
  ProfileStore &store = ProfileStore::getInstance();
  if (!store.needsFactoryProfile()) {
    return true; // the flash carries a profile; RAM is the caller's business
  }

  // The body is the compiled defaults, not _config: _config can still describe
  // a profile that an erase just removed, and Profile0 is by definition the
  // compiled-in baseline. cap is the size of the buffer, not a body limit.
  const uint16_t jsonLen = serializeProfile(
      kConfigDefaults, reinterpret_cast<char *>(s_staging),
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
  _activeId = 0;

  LOG_INFO("ConfigManager: factory Profile0 written, %u bytes", jsonLen);
  return true;
}

bool ConfigManager::loadProfile(uint16_t profileId) {
  ProfileStore &store = ProfileStore::getInstance();
  if (profileId != _activeId) {
    if (!store.selectProfile(profileId))
      return false;
    // The select above wrote the BootMeta entry that names this profile active,
    // so _activeId follows it and is not rolled back when the read below fails:
    // a reboot's scan reaches the same id. What a failed read leaves behind is
    // _config at the compiled defaults, which the reset below applies whichever
    // way the read goes.
    _activeId = profileId;
  }

  // The reset does not depend on what the read returns: _config is overwritten
  // with the compiled-in defaults before the read, and a body of length 0
  // (erased or half-written sector) leaves it at those defaults. Only the parse
  // step below looks at len.
  _config = kConfigDefaults;

  ProfileText text;
  bool ok = store.readProfile(PROFILE_ID_ACTIVE, &text);
  if (ok && text.len > 0) {
    parseProfile(text.data, &_config);
  }
  LOG_INFO("ConfigManager: load id=%u %s", profileId, ok ? "OK" : "FAIL");
  return ok;
}

bool ConfigManager::saveProfile() {
  ProfileStore &store = ProfileStore::getInstance();
  if (_activeId == 0) {
    LOG_WARN("ConfigManager: cannot save to factory Profile0");
    return false;
  }

  // The whole staging buffer is offered, so the largest body the flash layer
  // accepts (PROFILE_JSON_MAX) still fits with its terminator. A length of 0
  // means the serializer produced no usable body — there is nothing to persist.
  const uint16_t jsonLen = serializeProfile(
      _config, reinterpret_cast<char *>(s_staging),
      static_cast<uint16_t>(PROFILE_STAGING_SIZE));
  if (jsonLen == 0) {
    LOG_ERROR("ConfigManager: nothing to save, serialization produced no body");
    return false;
  }

  if (!store.modifyProfile(_activeId, reinterpret_cast<const char *>(s_staging),
                           jsonLen)) {
    LOG_ERROR("ConfigManager: save failed id=%u", _activeId);
    return false;
  }

  LOG_INFO("ConfigManager: save OK id=%u, len=%u", _activeId, jsonLen);
  return true;
}

uint8_t ConfigManager::profileCount() const {
  return ProfileStore::getInstance().getStatus().profileCount;
}

bool ConfigManager::selectProfile(uint16_t pid) { return loadProfile(pid); }

Profile::ProfileStatus ConfigManager::getStatus() const {
  return ProfileStore::getInstance().getStatus();
}

#else // THETAGP_CFG_HAS_FLASH

// Without a flash chip there is nowhere to keep profiles, so the configuration
// runs on the compiled defaults in ConfigStore. The host holds the user's
// settings and is expected to push them over the control protocol; nothing
// survives a power cycle on this side.

bool ConfigManager::init() {
  _config = kConfigDefaults;
  LOG_INFO("ConfigManager: no flash chip, running on defaults");
  return true;
}

bool ConfigManager::loadProfile(uint16_t) { return false; }

bool ConfigManager::saveProfile() { return false; }

uint8_t ConfigManager::profileCount() const { return 1; }

bool ConfigManager::selectProfile(uint16_t) { return false; }

Profile::ProfileStatus ConfigManager::getStatus() const { return {}; }

#endif // THETAGP_CFG_HAS_FLASH

} // namespace ThetaGP::Gamepad::Config
