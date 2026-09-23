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

#include "BoardConfig.h"
#include "conf/ThetaGP_Config.h" // THETAGP_CFG_HAS_FLASH, the flash switch the firmware reads

#include "gamepad/config/config_store.h"
#include "gamepad/profile/profile_store.h"

#include <cstdint>

namespace ThetaGP::Gamepad::Config {

class ConfigManager {
public:
  static ConfigManager &getInstance();

  bool init();
  bool loadProfile(uint16_t profileId);

  /** Write the configuration in effect to the profile it belongs to.
   *
   * `droppedKeys`, when it is not null, is handed what the write did not carry
   * over from the body it replaced: the number of object keys of that body the
   * body written in its place does not hold (Json::missingKeyCount, which is
   * where the count is defined). The body compared against is the one of the
   * profile this call writes to, read back by that id — not whichever profile
   * the store calls active, which a host upload moves on its own. A write that
   * carried every key over leaves it at 0, and so does every call that returns
   * without writing: the count is only ever raised by a write that reached the
   * flash, so a caller reports it as a fact of a save that happened. It stays 0
   * when the body being replaced cannot be read back, and when the comparison
   * has no answer (Json::missingKeyCount): a write that happened is not evidence
   * of one that dropped nothing. */
  bool saveProfile(uint32_t *droppedKeys = nullptr);
  uint16_t activeProfileId() const;
  uint8_t profileCount() const;

  /** Get const reference to active configuration. */
  const ConfigStore &config() const { return _config; }
  /** Get mutable reference to active configuration (internal use). */
  ConfigStore &configMut() { return _config; }

  bool selectProfile(uint16_t profileId);
  void setActiveProfileId(uint16_t id) { _activeId = id; }
  Profile::ProfileStatus getStatus() const;

#if THETAGP_CFG_HAS_FLASH
  /** Profile storage on the external flash; absent on boards without a chip. */
  Profile::ProfileStore &store() {
    return Profile::ProfileStore::getInstance();
  }

  /** Bring flash and RAM back in step on a flash that carries no profile at
   * all: write the factory Profile0 (the compiled defaults) and reset the
   * active configuration to what that body holds. Does nothing when the flash
   * already carries a profile.
   *
   * Shared by init() and by the test.chip_erase post-path: an erase changes the
   * answer without a reboot, so the question is asked of the flash instead of
   * being decided once at boot. Returns true when the flash holds a profile
   * after the call. */
  bool ensureFactoryProfile();
#endif

private:
  ConfigManager() = default;
  ConfigStore _config;
  uint16_t _activeId = 0;
};

} // namespace ThetaGP::Gamepad::Config
