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
#include "conf/ThetaGP_Config.h" // THETAGP_CFG_HAS_FLASH, the flash switch this file branches on
#include "test/dispatcher.h"
#include "test/framelayer.h"

#include "taskmanager.h"

#include "gamepad/profile/profile_store.h"

#include "utils/log/log.h"
#include "utils/meminfo.h"

#include "protocol/proto.h"
#include "protocol/proto_resp.h"

#include "tusb.h"
#include <cstring>
#include <type_traits>

// The response table of sys.get_task_info (protocol/proto_resp.h) carries the
// task counters only in a build that compiles them in, and it takes that from
// the roles protocol.toml marks runCount / lateCount with: for role =
// "task_counters" the header defines THETAGP_RESP_HAS_TASK_COUNTERS out of
// USE_TASK_COUNTERS, and writes the presence of those two entries from it.
// Both headers are included above, conf/ThetaGP_Config.h before proto_resp.h,
// so the two names below are the ones this build has.
//
// The generator's half of that binding is a macro name: the header opens with
// `#ifdef <the guard scripts/gen_proto.py names for the role>`. The halves can
// disagree with nothing failing — with no role on those fields in
// protocol.toml the header carries no flag at all, the entries take the
// constant presence 1, and a build that does not compile the counters in
// answers runCount and lateCount with zeros, under keys that read as measured.
// The checks below are this translation unit's half: the switch and the flag
// agree, in either direction, or nothing here compiles.
#if defined(USE_TASK_COUNTERS) && !(defined(THETAGP_RESP_HAS_TASK_COUNTERS) \
                                   && THETAGP_RESP_HAS_TASK_COUNTERS)
#error "[task_counters] USE_TASK_COUNTERS is on but proto_resp.h reports no task counters — mark runCount / lateCount role=\"task_counters\" in protocol.toml"
#endif
#if !defined(USE_TASK_COUNTERS) && !(defined(THETAGP_RESP_HAS_TASK_COUNTERS) \
                                    && !THETAGP_RESP_HAS_TASK_COUNTERS)
#error "[task_counters] USE_TASK_COUNTERS is off but proto_resp.h reports task counters — the flag comes from the guard gen_proto.py names for role=\"task_counters\""
#endif

// Expands a response field table (protocol/proto_resp.h) into the field writes
// of a response. The JSON key and the printf conversion of a field come from
// the table, and its value from the THETAGP_VALUE_<name> macro that the table's
// name selects: a name the table carries and this file does not define, and a
// value whose type is not the one the table declares, are both compile errors,
// and no value can land under another field's key because nothing is paired by
// position. A table is invoked as TABLE(THETAGP_RESP_FIELD), one invocation per
// response, with no semicolon after it — each expansion ends itself.
#define THETAGP_RESP_FIELD(name, type, spec, presence)                          \
    static_assert(std::is_same<decltype(THETAGP_VALUE_##name), type>::value,    \
                  #name ": value type differs from protocol.toml");             \
    if (presence) {                                                             \
        resp.printf("," #name ":" spec, THETAGP_VALUE_##name);                  \
    }

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

// ── sys.get_fw_version ──
// Response field values, keyed by the name protocol.toml declares them with.
// The order and the conversions come from THETAGP_RESP_SYS_GET_FW_VERSION,
// expanded in the handler below.
#define THETAGP_VALUE_board      ((const char *)BOARD_NAME)
#define THETAGP_VALUE_version    ((const char *)THETAGP_FW_VERSION)
#define THETAGP_VALUE_build_date ((const char *)__DATE__)
#define THETAGP_VALUE_build_time ((const char *)__TIME__)

static void handleSysGetFwVersion([[maybe_unused]] const char *cmd,
                                  [[maybe_unused]] const Json &json) {
    int queued = json.getInt("queued");
    Json resp;
    resp.beginWrite(s_sysRespBuf, sizeof(s_sysRespBuf));
    resp.printf("{status:%Q,cmd:%Q,queued:%d", "ok", "sys.get_fw_version",
                queued + 1);
    THETAGP_RESP_SYS_GET_FW_VERSION(THETAGP_RESP_FIELD)
    resp.printf("}");
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
        // The moving sums carry execution and delta time scaled by 10, so the
        // per-task averages are in tenths of a microsecond.
        uint32_t avgExecUs = info->averageExecutionTime10thUs / 10U;
        uint32_t avgDeltaUs = info->averageDeltaTime10thUs / 10U;

        // Response field values, keyed by the name protocol.toml declares them
        // with. The order and the conversions come from
        // THETAGP_RESP_SYS_GET_TASK_INFO, expanded below. The run counters are
        // copied into TaskInfo only in a build that compiles them in, and the
        // table reports them only there: without them there is no value to
        // read, so the unused value is a zero.
#define THETAGP_VALUE_tid          ((int32_t)tid)
#define THETAGP_VALUE_name         ((const char *)info->taskName)
#define THETAGP_VALUE_sub          ((const char *)info->subTaskName)
#define THETAGP_VALUE_desiredUs    ((uint32_t)info->desiredPeriodUs)
#define THETAGP_VALUE_avgCycleUs   ((uint32_t)avgUs)
#define THETAGP_VALUE_actualHz     ((uint32_t)actualHz)
#define THETAGP_VALUE_maxExecUs    ((uint32_t)info->maxExecutionTimeUs)
#define THETAGP_VALUE_avgExecUs    ((uint32_t)avgExecUs)
#define THETAGP_VALUE_totalExecUs  ((uint32_t)info->totalExecutionTimeUs)
#define THETAGP_VALUE_avgDeltaUs   ((uint32_t)avgDeltaUs)
#ifdef USE_TASK_COUNTERS
#define THETAGP_VALUE_runCount ((uint32_t)info->runCount)
#define THETAGP_VALUE_lateCount ((uint32_t)info->lateCount)
#else
#define THETAGP_VALUE_runCount ((uint32_t)0)
#define THETAGP_VALUE_lateCount ((uint32_t)0)
#endif

        resp.printf("{status:%Q,cmd:%Q,queued:%d", "ok", "sys.get_task_info",
                    queued + 1);
        THETAGP_RESP_SYS_GET_TASK_INFO(THETAGP_RESP_FIELD)
        resp.printf("}");
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
// Aggregate resource report, four-item scope: CPU load, MCU flash,
// MCU RAM (per region + aggregate + reserve), external SPI flash. Bytes on the
// MCU side, sectors on the external flash, no percentages.

static void handleSysGetUsage([[maybe_unused]] const char *cmd,
                              [[maybe_unused]] const Json &json) {
    int queued = json.getInt("queued");

    using namespace ThetaGP::Util::MemInfo;

#if THETAGP_CFG_HAS_FLASH
    auto &profileStore = ThetaGP::Gamepad::Profile::ProfileStore::getInstance();
    ProfileStatus pstat = profileStore.getStatus();
#else
    ProfileStatus pstat{};
#endif

    Json resp;
    resp.beginWrite(s_sysRespBuf, sizeof(s_sysRespBuf));

    // Response field values, keyed by the name protocol.toml declares them
    // with. The order and the conversions come from
    // THETAGP_RESP_SYS_GET_USAGE, expanded below.
#define THETAGP_VALUE_cpu_load_percent                                            \
    ((uint32_t)Gamepad::TaskManager::getAverageSystemLoadPercent())
#define THETAGP_VALUE_task_count ((uint32_t)Gamepad::TaskManager::getTaskCount())
#define THETAGP_VALUE_mcu_flash_used_bytes ((uint32_t)mcuFlashUsedBytes())
#define THETAGP_VALUE_mcu_flash_total_bytes ((uint32_t)mcuFlashTotalBytes())
#define THETAGP_VALUE_ram_used_bytes ((uint32_t)ramUsedBytes())
#define THETAGP_VALUE_ram_total_bytes ((uint32_t)ramTotalBytes())
#define THETAGP_VALUE_ram_reserved_bytes ((uint32_t)ramReservedBytes())
#define THETAGP_VALUE_ram_dtcm_used_bytes ((uint32_t)region(RegionId::Dtcm).used)
#define THETAGP_VALUE_ram_axi_used_bytes ((uint32_t)region(RegionId::Axi).used)
#define THETAGP_VALUE_ram_d2_used_bytes ((uint32_t)region(RegionId::D2).used)
#define THETAGP_VALUE_ram_d3_used_bytes ((uint32_t)region(RegionId::D3).used)
#define THETAGP_VALUE_ram_itcm_used_bytes ((uint32_t)region(RegionId::Itcm).used)
#define THETAGP_VALUE_ext_flash_total_sectors ((uint32_t)pstat.totalSectors)
#define THETAGP_VALUE_ext_flash_used_sectors ((uint32_t)pstat.usedSectors)
#define THETAGP_VALUE_ext_flash_free_sectors ((uint32_t)pstat.freeSectors)
#define THETAGP_VALUE_ext_flash_reserved_sectors ((uint32_t)pstat.reservedSectors)
#define THETAGP_VALUE_profile_count ((uint32_t)pstat.profileCount)

    resp.printf("{status:%Q,cmd:%Q,queued:%d", "ok", "sys.get_usage", queued + 1);
    THETAGP_RESP_SYS_GET_USAGE(THETAGP_RESP_FIELD)
    resp.printf("}");
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
