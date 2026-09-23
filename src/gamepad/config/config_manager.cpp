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
#include "utils/json/json.h" // Json::missingKeyCount, the key comparison

#include "utils/log/log.h"

#include <cstring> // memcpy, copying the body being replaced out of the store

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
  // The window is text.len, the body length readProfile reported for the bytes
  // it put in text.data — the parse never leans on a terminator the buffer may
  // not hold.
  if (text.len > 0) {
    parseProfile(text.data, text.len, &_config);
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
  // text.len is the length of the body the read placed in text.data, so it is
  // the parse window here too.
  if (ok && text.len > 0) {
    parseProfile(text.data, text.len, &_config);
  }
  LOG_INFO("ConfigManager: load id=%u %s", profileId, ok ? "OK" : "FAIL");
  return ok;
}

// ── saveProfile() ──
// The body this save replaces, read back before the write: a save that does not
// carry a key of it over says so instead of dropping it in silence.
//
// A buffer of its own, and read before the body being written is serialized:
// the read the profile store offers goes through its staging buffer, which the
// serializer's output lives in, so it has to happen first and be copied out.
static COMMON_ZERO_INIT uint8_t s_replaced[PROFILE_STAGING_SIZE];

bool ConfigManager::saveProfile(uint32_t *droppedKeys) {
  ProfileStore &store = ProfileStore::getInstance();

  // What the caller is handed is raised by the write below and by nothing
  // else, so every path that returns before it leaves this at 0.
  if (droppedKeys) {
    *droppedKeys = 0;
  }

  if (_activeId == 0) {
    LOG_WARN("ConfigManager: cannot save to factory Profile0");
    return false;
  }

  // How many keys of the body being replaced the new body does not carry. The
  // replaced body is the one of the profile this save writes to, read by its id
  // rather than as "the active one": createProfile moves the store's active id
  // on its own — a profile the host just uploaded becomes the active one while
  // this layer still writes the profile it has in hand — and a count taken
  // against that other body describes a replacement that is not this one. Its
  // keys are looked up in the body about to be written rather than in a list of
  // the keys the serializer writes, so a key added to the profile shape moves
  // this count with it.
  bool haveReplaced = false;
  uint16_t replacedLen = 0;
  if (droppedKeys) {
    ProfileText replaced;
    if (store.readProfile(_activeId, &replaced) && replaced.len > 0) {
      // readBody reports at most PROFILE_JSON_MAX bytes, one below the size of
      // this buffer.
      replacedLen = replaced.len;
      memcpy(s_replaced, replaced.data, replacedLen);
      haveReplaced = true;
    } else {
      // Nothing to compare against. The write below still happens; what it does
      // not carry over stays unreported rather than reported as none.
      LOG_WARN("ConfigManager: the body being replaced could not be read; the "
               "save cannot say what it does not carry over");
    }
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

  uint32_t notCarriedOver = 0;
  if (droppedKeys && haveReplaced) {
    Json fresh;
    fresh.parse(reinterpret_cast<const char *>(s_staging), jsonLen);
    Json replaced;
    replaced.parse(reinterpret_cast<const char *>(s_replaced), replacedLen);
    notCarriedOver = fresh.missingKeyCount(replaced);
  }

  if (!store.modifyProfile(_activeId, reinterpret_cast<const char *>(s_staging),
                           jsonLen)) {
    LOG_ERROR("ConfigManager: save failed id=%u", _activeId);
    return false;
  }

  // Handed over only after the write reached the flash: the count is a fact of
  // the save that happened, not of the one that was prepared.
  if (droppedKeys) {
    *droppedKeys = notCarriedOver;
  }

  LOG_INFO("ConfigManager: save OK id=%u, len=%u, dropped=%u", _activeId,
           jsonLen, static_cast<unsigned>(notCarriedOver));
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
