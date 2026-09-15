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

#include "gamepad/config/config_store.h"
#include "gamepad/profile/profile_store.h"

#include <cstdint>

namespace ThetaGP::Gamepad::Config {

class ConfigManager {
public:
  static ConfigManager &getInstance();

  bool init();
  bool loadProfile(uint16_t profileId);
  bool saveProfile();
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
#endif

private:
  ConfigManager() = default;
  ConfigStore _config;
  uint16_t _activeId = 0;
};

} // namespace ThetaGP::Gamepad::Config
