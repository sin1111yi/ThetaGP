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

#include "taskmanager.h"

#include "drivers/device/devmem.h"
#include "gamepad/profile/profile_store.h"

#include "utils/log/log.h"
#include "utils/mempool/mempoolmanager.h"

#include "protocol/proto.h"

#include <cstring>
#include "tusb.h"

namespace ThetaGP::Test {

using namespace ThetaGP::Gamepad::Profile;

// Firmware version string (matches CMake THETAGP_VERSION)
static constexpr const char *THETAGP_FW_VERSION = "0.1.1";

// NVIC_SystemReset from CMSIS core
#include "build_info.h"

SysHandler &SysHandler::getInstance() {
    static SysHandler instance;
    return instance;
}

// Staging buffer for building response JSON
static COMMON_ZERO_INIT char s_sysRespBuf[2048];

// ---------------------------------------------------------------------------
// Handler functions (CommandHandler signature)
// ---------------------------------------------------------------------------

static void handleSysPing([[maybe_unused]] const char *cmd,
                          [[maybe_unused]] const Json &json) {
    int queued = json.getInt("queued");
    Json resp;
    resp.beginWrite(s_sysRespBuf, sizeof(s_sysRespBuf));
    resp.printf("{status:%Q,cmd:%Q,queued:%d}",
                "ok", "sys.ping", queued + 1);
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

static void handleSysGetFwVersion([[maybe_unused]] const char *cmd,
                                  [[maybe_unused]] const Json &json) {
    int queued = json.getInt("queued");
    Json resp;
    resp.beginWrite(s_sysRespBuf, sizeof(s_sysRespBuf));
    resp.printf("{status:%Q,cmd:%Q,queued:%d,board:%Q,version:%Q,"
                "build_date:%Q,build_time:%Q}",
                "ok", "sys.get_fw_version", queued + 1,
                BOARD_NAME, THETAGP_FW_VERSION, __DATE__, __TIME__);
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

[[noreturn]] static void handleSysReset([[maybe_unused]] const char *cmd,
                                         [[maybe_unused]] const Json &json) {
    int queued = json.getInt("queued");
    Json resp;
    resp.beginWrite(s_sysRespBuf, sizeof(s_sysRespBuf));
    resp.printf("{status:%Q,cmd:%Q,queued:%d,info:%Q}",
                "ok", "sys.reset", queued + 1, "resetting...");
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
    // Small delay to allow the response to be sent
    for (volatile uint32_t i = 0; i < 100000; ++i) {}
    NVIC_SystemReset();
}

static void handleSysGetTaskInfo([[maybe_unused]] const char *cmd,
                                 const Json &json) {
    int queued = json.getInt("queued");
    int tid = json.getInt("tid");
    Json resp;
    resp.beginWrite(s_sysRespBuf, sizeof(s_sysRespBuf));

    const auto *info = Gamepad::TaskManager::getTaskInfo(tid);
    if (info) {
        // ArduinoJson printf does not support float, use integer math
        uint32_t avgUs = static_cast<uint32_t>(info->movingAverageCycleTimeUs);
        uint32_t actualHz = (avgUs > 0) ? (1000000U / avgUs) : 0;
        resp.printf("{status:%Q,cmd:%Q,queued:%d,tid:%d,"
                    "name:%Q,sub:%Q,desiredUs:%lu,"
                    "avgCycleUs:%lu,actualHz:%lu,maxExecUs:%lu}",
                    "ok", "sys.get_task_info", queued + 1, tid,
                    info->taskName, info->subTaskName,
                    info->desiredPeriodUs,
                    (unsigned long)avgUs,
                    (unsigned long)actualHz,
                    info->maxExecutionTimeUs);
    } else {
        resp.printf("{status:%Q,cmd:%Q,queued:%d,tid:%d,"
                    "error_code:%d,reason:%Q}",
                    "error", "sys.get_task_info", queued + 1, tid,
                    static_cast<int>(Proto::ErrorCode::ERR_INVALID_PARAM),
                    "invalid TID");
    }
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

static void handleSysEnterDfu([[maybe_unused]] const char *cmd,
                              [[maybe_unused]] const Json &json) {
    int queued = json.getInt("queued");
    Json resp;
    resp.beginWrite(s_sysRespBuf, sizeof(s_sysRespBuf));
    resp.printf("{status:%Q,cmd:%Q,queued:%d,error_code:%d,reason:%Q}",
                "error", "sys.enter_dfu", queued + 1,
                static_cast<int>(Proto::ErrorCode::ERR_NOT_SUPPORTED),
                "DFU not yet implemented");
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

// ── sys.get_usage ──
// Aggregate system resource report: CPU load, memory pool, task count,
// SPI flash usage (from profile store).

static void handleSysGetUsage([[maybe_unused]] const char *cmd,
                              [[maybe_unused]] const Json &json) {
    int queued = json.getInt("queued");

    using namespace ThetaGP::Mempool;
    auto &dm = Drivers::Device::DevMem::getInstance();
    auto pid = dm.poolId();
    auto poolStats = (pid != INVALID_POOL_ID)
                         ? MempoolManager::poolStats(pid)
                         : PoolStats{0, 0, 0, 0, 0};

    auto &profileStore = ThetaGP::Gamepad::Profile::ProfileStore::getInstance();
    ProfileStatus pstat = profileStore.getStatus();

    Json resp;
    resp.beginWrite(s_sysRespBuf, sizeof(s_sysRespBuf));
    resp.printf(
        "{status:%Q,cmd:%Q,queued:%d,"
        "cpu_load_percent:%u,"
        "task_count:%u,"
        "mem_pool_id:%d,mem_pool_total:%u,mem_pool_used:%u,mem_pool_free:%u,"
        "profile_count:%d,flash_total_sectors:%lu,flash_used_sectors:%lu,"
        "flash_free_sectors:%lu}",
        "ok", "sys.get_usage", queued + 1,
        (unsigned)Gamepad::TaskManager::getAverageSystemLoadPercent(),
        (unsigned)Gamepad::TaskManager::getTaskCount(),
        static_cast<int>(pid),
        poolStats.totalSize, poolStats.usedSize, poolStats.freeSize,
        pstat.profileCount,
        (unsigned long)pstat.totalSectors,
        (unsigned long)pstat.usedSectors,
        (unsigned long)pstat.freeSectors);
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
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
    Proto::registerSysGetTaskInfo(handleSysGetTaskInfo);
    Proto::registerSysGetUsage(handleSysGetUsage);
}

// ---------------------------------------------------------------------------
// Main dispatch — delegates to Proto-generated dispatch table
// ---------------------------------------------------------------------------
void SysHandler::handle(const char *cmd, const Json &json) {
    LOG_DEBUG("SysHandler: cmd='%s'", cmd);

    // Direct dispatch, bypass Proto generated table
    if (strcmp(cmd, "sys.ping") == 0) {
        handleSysPing(cmd, json);
    } else if (strcmp(cmd, "sys.get_fw_version") == 0) {
        handleSysGetFwVersion(cmd, json);
    } else if (strcmp(cmd, "sys.reset") == 0) {
        handleSysReset(cmd, json);
    } else if (strcmp(cmd, "sys.enter_dfu") == 0) {
        handleSysEnterDfu(cmd, json);
    } else if (strcmp(cmd, "sys.get_task_info") == 0) {
        handleSysGetTaskInfo(cmd, json);
    } else if (strcmp(cmd, "sys.get_usage") == 0) {
        handleSysGetUsage(cmd, json);
    } else {
        // Unknown sys command — return error response
        int queued = json.getInt("queued");
        Json resp;
        resp.beginWrite(s_sysRespBuf, sizeof(s_sysRespBuf));
        resp.printf("{status:%Q,cmd:%Q,queued:%d,error_code:%d,reason:%Q}",
                    "error", cmd, queued + 1,
                    static_cast<int>(Proto::ErrorCode::ERR_UNKNOWN_CMD),
                    "unknown command");
        uint16_t len = resp.end();
        FrameLayer::getInstance().sendResponse(resp.c_str(), len);
    }
}

} // namespace ThetaGP::Test
