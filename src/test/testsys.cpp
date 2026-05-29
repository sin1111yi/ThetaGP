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

#include "test/testsys.h"
#include "test/framelayer.h"

#include "utils/log/log.h"

#include <cstring>

namespace ThetaGP::Test {

#ifdef THETAGP_ENABLE_TEST_API

// Firmware version string (matches CMake THETAGP_VERSION)
static constexpr const char *THETAGP_FW_VERSION = "0.1.1";

// NVIC_SystemReset from CMSIS core
#include "stm32h7xx.h"

SysHandler &SysHandler::getInstance() {
    static SysHandler instance;
    return instance;
}

void SysHandler::handle(const char *cmd, JsonDocument &doc) {
    int queued = doc["queued"].as<int>();
    LOG_DEBUG("SysHandler: cmd='%s' queued=%d", cmd, queued);

    if (strcmp(cmd, "sys.ping") == 0) {
        JsonDocument resp;
        resp["status"] = "ok";
        resp["cmd"] = "sys.ping";
        resp["queued"] = queued + 1;
        FrameLayer::getInstance().sendResponse(resp);
        LOG_DEBUG("SysHandler: ping responded");

    } else if (strcmp(cmd, "sys.get_fw_version") == 0) {
        JsonDocument resp;
        resp["status"] = "ok";
        resp["cmd"] = "sys.get_fw_version";
        resp["queued"] = queued + 1;
        resp["board"] = BOARD_NAME;
        resp["version"] = THETAGP_FW_VERSION;
        resp["build_date"] = __DATE__;
        resp["build_time"] = __TIME__;
        FrameLayer::getInstance().sendResponse(resp);
        LOG_DEBUG("SysHandler: firmware version reported");

    } else if (strcmp(cmd, "sys.reset") == 0) {
        JsonDocument resp;
        resp["status"] = "ok";
        resp["cmd"] = "sys.reset";
        resp["queued"] = queued + 1;
        FrameLayer::getInstance().sendResponse(resp);
        LOG_INFO("SysHandler: system reset requested");
        NVIC_SystemReset();

    } else if (strcmp(cmd, "sys.enter_dfu") == 0) {
        JsonDocument resp;
        resp["status"] = "error";
        resp["cmd"] = "sys.enter_dfu";
        resp["queued"] = queued + 1;
        resp["error_code"] = 6;
        resp["reason"] = "DFU mode not supported yet";
        FrameLayer::getInstance().sendResponse(resp);
        LOG_WARN("SysHandler: DFU mode requested but not supported");

    } else {
        LOG_WARN("SysHandler: Unknown sys command: %s", cmd);
        JsonDocument resp;
        resp["status"] = "error";
        resp["cmd"] = cmd;
        resp["queued"] = queued + 1;
        resp["error_code"] = 1;
        resp["reason"] = "unknown command";
        FrameLayer::getInstance().sendResponse(resp);
    }
}

#endif // THETAGP_ENABLE_TEST_API

} // namespace ThetaGP::Test
