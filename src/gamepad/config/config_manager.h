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

  // Reserved for a later entry of this layer: not implemented, so it reads
  // nothing and answers that it did not.
  bool loadProfile(uint16_t profileId);

  // Reads a profile's body into the configuration the device runs on, over the
  // compiled defaults, and leaves the active profile where it is. A read that
  // fails and a body this firmware does not read both answer false, leaving the
  // configuration at those defaults.
  bool readProfileBody(uint16_t profileId);

  // Replaces the configuration in effect with the compiled defaults, touching no
  // storage: writing the result out is saveProfile's job.
  void factoryReset();

  // Writes the configuration in effect to the profile the store carries as
  // active, read once as the call starts.
  //
  // `droppedKeys`, when it is not null, is handed the number of keys of the
  // replaced body the new one does not carry (Util::Json::missingKeyCount). It stays 0
  // for a call that did not reach the flash, and for one whose comparison had no
  // answer.
  bool saveProfile(uint32_t *droppedKeys = nullptr);

  // The profile the configuration belongs to, as the store carries it.
  uint16_t activeProfileId() const;
  uint8_t profileCount() const;

  // Hands the fields that reach past the store to the code reading them: the
  // threshold the logger writes at. Applied when a profile is read, on reset, and
  // after a write.
  void applySys();

  // The configuration in effect.
  const ConfigStore &config() const { return _config; }
  // The same configuration, writable by this layer's writers.
  ConfigStore &configMut() { return _config; }

  // Reserved for a later entry of this layer: not implemented, so it moves
  // nothing.
  bool selectProfile(uint16_t profileId);
  // Reserved for a later entry of this layer: not implemented, so it moves
  // nothing.
  void setActiveProfileId(uint16_t id);
  Profile::ProfileStatus getStatus() const;

#if THETAGP_CFG_HAS_FLASH
  // Profile storage on the external flash; absent on boards without a chip.
  Profile::ProfileStore &store() {
    return Profile::ProfileStore::getInstance();
  }

  // Writes the factory Profile 0 where the flash carries no profile, and resets
  // the configuration to what that body holds. The question is put to the flash
  // rather than decided once at boot, so it holds whenever the profile area has
  // been left empty. Answers whether the flash holds a profile after the call.
  bool ensureFactoryProfile();
  // Writes the factory body again from the compiled defaults when the one held
  // is of an older format.
  bool repairFactoryProfile();
#endif

private:
  ConfigManager() = default;
  ConfigStore _config;
};

} // namespace ThetaGP::Gamepad::Config
