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

#include "utils/json/json.h"

namespace ThetaGP::Test {

/**
 * Handles the `config.` domain commands over the CDC ACM test channel.
 * Reads and writes the configuration in effect through ConfigManager, and
 * takes the set of keys, their types and their ranges from the key table
 * (gamepad/config/key_table.h) rather than from a list of its own.
 */
class ConfigCmdHandler {
public:
  ConfigCmdHandler() = default;
  ConfigCmdHandler(const ConfigCmdHandler &) = delete;
  ConfigCmdHandler &operator=(const ConfigCmdHandler &) = delete;

  static ConfigCmdHandler &getInstance();
  static void handleConfig(const char *cmd, const Json &json);
  static void registerHandlers();
};

} // namespace ThetaGP::Test
