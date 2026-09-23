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
 * SysHandler -- handles commands in the `sys.` domain.
 *
 * Supported commands:
 *   sys.ping           - Health check, responds with status=ok
 *   sys.get_fw_version - Read firmware version from build info
 *   sys.reset          - Trigger NVIC system reset
 *   sys.enter_dfu      - Enter DFU mode (not yet implemented)
 */
class SysHandler {
public:
    SysHandler() = default;
    SysHandler(const SysHandler &) = delete;
    SysHandler &operator=(const SysHandler &) = delete;

    static SysHandler &getInstance();
    static void handle(const char *cmd, const Json &json);
    static void registerHandlers();
};

} // namespace ThetaGP::Test
