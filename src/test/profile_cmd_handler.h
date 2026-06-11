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

#include <ArduinoJson.h>

namespace ThetaGP::Test {

/**
 * Handles the `profile.` domain commands over the CDC ACM test channel.
 * Delegates to ProfileStore and ConfigStore for storage and serialization.
 */
class ProfileCmdHandler {
public:
  ProfileCmdHandler() = default;
  ProfileCmdHandler(const ProfileCmdHandler &) = delete;
  ProfileCmdHandler &operator=(const ProfileCmdHandler &) = delete;

  static ProfileCmdHandler &getInstance();
  static void handleProfile(const char *cmd, JsonDocument &doc);
  static void registerHandlers();
};

} // namespace ThetaGP::Test
