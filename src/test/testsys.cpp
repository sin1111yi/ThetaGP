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
#include "test/dispatcher.h"
#include "test/framelayer.h"

#include "utils/log/log.h"

#include "protocol/proto.h"

#include <cstring>

namespace ThetaGP::Test {

// Firmware version string (matches CMake THETAGP_VERSION)
static constexpr const char *THETAGP_FW_VERSION = "0.1.1";

// NVIC_SystemReset from CMSIS core
#include "build_info.h"

SysHandler &SysHandler::getInstance() {
    static SysHandler instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Handler functions (CommandHandler signature)
// ---------------------------------------------------------------------------

static void handleSysPing([[maybe_unused]] const char *cmd,
                          [[maybe_unused]] JsonDocument &doc) {
    JsonDocument resp;
    resp["status"] = "ok";
    resp["cmd"] = "sys.ping";
    int queued = doc["queued"].as<int>();
    resp["queued"] = queued + 1;
    FrameLayer::getInstance().sendResponse(resp);
}

static void handleSysGetFwVersion([[maybe_unused]] const char *cmd,
                                  [[maybe_unused]] JsonDocument &doc) {
    JsonDocument resp;
    resp["status"] = "ok";
    resp["cmd"] = "sys.get_fw_version";
    int queued = doc["queued"].as<int>();
    resp["queued"] = queued + 1;
    resp["board"] = BOARD_NAME;
    resp["version"] = THETAGP_FW_VERSION;
    resp["build_date"] = __DATE__;
    resp["build_time"] = __TIME__;
    FrameLayer::getInstance().sendResponse(resp);
}

[[noreturn]] static void handleSysReset([[maybe_unused]] const char *cmd,
                                         [[maybe_unused]] JsonDocument &doc) {
    JsonDocument resp;
    resp["status"] = "ok";
    resp["cmd"] = "sys.reset";
    int queued = doc["queued"].as<int>();
    resp["queued"] = queued + 1;
    resp["info"] = "resetting...";
    FrameLayer::getInstance().sendResponse(resp);
    // Small delay to allow the response to be sent
    for (volatile uint32_t i = 0; i < 100000; ++i) {}
    NVIC_SystemReset();
}

static void handleSysEnterDfu([[maybe_unused]] const char *cmd,
                              [[maybe_unused]] JsonDocument &doc) {
    JsonDocument resp;
    resp["status"] = "error";
    resp["cmd"] = "sys.enter_dfu";
    int queued = doc["queued"].as<int>();
    resp["queued"] = queued + 1;
    resp["error_code"] = static_cast<uint8_t>(Proto::ErrorCode::ERR_NOT_SUPPORTED);
    resp["reason"] = "DFU not yet implemented";
    FrameLayer::getInstance().sendResponse(resp);
}

// ---------------------------------------------------------------------------
// registerHandlers — self-register with the Dispatcher and Proto
// ---------------------------------------------------------------------------
void SysHandler::registerHandlers() {
    Dispatcher::getInstance().registerHandler("sys", SysHandler::handle);
    Proto::registerSysPing(handleSysPing);
    Proto::registerSysGetFwVersion(handleSysGetFwVersion);
    Proto::registerSysReset(handleSysReset);
    Proto::registerSysEnterDfu(handleSysEnterDfu);
}

// ---------------------------------------------------------------------------
// Main dispatch — delegates to Proto-generated dispatch table
// ---------------------------------------------------------------------------
void SysHandler::handle(const char *cmd, JsonDocument &doc) {
    int queued = doc["queued"].as<int>();
    LOG_DEBUG("SysHandler: cmd='%s' queued=%d", cmd, queued);
    Proto::dispatch(cmd, doc);
}

} // namespace ThetaGP::Test
