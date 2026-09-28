/*
 * This file is a part of ThetaGP.
 */

#include "comm/sys_handler.h"

#include <cstdio>

#include "comm/reply_error.h"
#include "conf/ThetaGP_Config.h"
#include "gamepad/profile/profile_store.h"
#include "task_manager.h"
#include "utils/mem_info.h"

namespace ThetaGP::Comm {
namespace {

// A fixed-size text field: what fits goes in, and it is always terminated.
void copyText(char *out, size_t capacity, const char *text) {
    if (capacity == 0) {
        return;
    }
    std::snprintf(out, capacity, "%s", text);
}

constexpr const char *kFirmwareVersion = "0.1.1";

constexpr size_t kMaxRegions =
    static_cast<size_t>(Util::MemInfo::RegionId::Count);

// The capacity of the reply's regions array, as the generated message declares
// it. The number is not restated here: the schema's field options declare a
// max_count for that field, nanopb writes it as the array's own length, and
// this reads that declaration back.
constexpr size_t kUsageRegionsCapacity =
    sizeof(ThetaGP_SysGetUsageOk::regions) /
    sizeof(ThetaGP_SysGetUsageOk::regions[0]);

// The usage arm writes one entry per memory region into that array, so it must
// have room for the enum's items less the flash — the one region it leaves
// out. A region added to the enum with no room left in the array would write
// past its end, and this is where the two counts are held against each other:
// the array's capacity must cover every region the arm reports.
static_assert(kMaxRegions - 1 <= kUsageRegionsCapacity,
              "the reply's regions array must have room for every memory "
              "region the usage arm reports");

} // namespace

void SysHandler::ping(ThetaGP_Reply &reply) {
    reply.which_kind = ThetaGP_Reply_sys_ping_tag;
}

void SysHandler::fwVersion(ThetaGP_Reply &reply) {
    reply.which_kind = ThetaGP_Reply_sys_get_fw_version_tag;
    ThetaGP_SysGetFwVersionOk &ok = reply.kind.sys_get_fw_version;
    copyText(ok.board, sizeof ok.board, BOARD_NAME);
    copyText(ok.version, sizeof ok.version, kFirmwareVersion);
    copyText(ok.build_date, sizeof ok.build_date, __DATE__);
    copyText(ok.build_time, sizeof ok.build_time, __TIME__);
}

void SysHandler::reset(ThetaGP_Reply &reply) {
    reply.which_kind = ThetaGP_Reply_sys_reset_tag;
}

void SysHandler::enterDfu(ThetaGP_Reply &reply) {
    writeFailure(reply, ThetaGP_ErrorCode_ERR_NOT_SUPPORTED,
                 ThetaGP_Reason_REASON_NOT_IMPLEMENTED);
}

void SysHandler::taskInfo(const ThetaGP_Request &request, ThetaGP_Reply &reply) {
    const int32_t tid = request.kind.sys_get_task_info.tid;
    const Gamepad::TaskInfo *info = Gamepad::TaskManager::getTaskInfo(tid);
    if (info == nullptr) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_INVALID_TASK_ID);
        return;
    }

    reply.which_kind = ThetaGP_Reply_sys_get_task_info_tag;
    ThetaGP_SysGetTaskInfoOk &ok = reply.kind.sys_get_task_info;
    const uint32_t avgUs =
        static_cast<uint32_t>(info->movingAverageCycleTimeUs);
    ok.tid = tid;
    copyText(ok.name, sizeof ok.name, info->taskName);
    copyText(ok.sub, sizeof ok.sub, info->subTaskName);
    ok.desired_us = info->desiredPeriodUs;
    ok.avg_cycle_us = avgUs;
    ok.actual_hz = (avgUs > 0) ? (1000000U / avgUs) : 0;
    ok.max_exec_us = info->maxExecutionTimeUs;
    ok.avg_exec_us = info->averageExecutionTime10thUs / 10U;
    ok.total_exec_us = info->totalExecutionTimeUs;
    ok.avg_delta_us = info->averageDeltaTime10thUs / 10U;
#if THETAGP_CFG_TASK_COUNTERS
    ok.has_run_count = true;
    ok.run_count = info->runCount;
    ok.has_late_count = true;
    ok.late_count = info->lateCount;
#endif
}

void SysHandler::usage(ThetaGP_Reply &reply) {
    using namespace Util::MemInfo;
    using Gamepad::TaskManager;

#if THETAGP_CFG_HAS_FLASH
    const Gamepad::Profile::ProfileStatus pstat =
        Gamepad::Profile::ProfileStore::getInstance().getStatus();
#else
    const Gamepad::Profile::ProfileStatus pstat{};
#endif

    reply.which_kind = ThetaGP_Reply_sys_get_usage_tag;
    ThetaGP_SysGetUsageOk &ok = reply.kind.sys_get_usage;
    ok.cpu_load_percent = TaskManager::getAverageSystemLoadPercent();
    ok.task_count = static_cast<uint32_t>(TaskManager::getTaskCount());
    ok.mcu_flash_used_bytes = mcuFlashUsedBytes();
    ok.mcu_flash_total_bytes = mcuFlashTotalBytes();
    ok.ram_used_bytes = ramUsedBytes();
    ok.ram_total_bytes = ramTotalBytes();
    ok.ram_reserved_bytes = ramReservedBytes();
    ok.ext_flash_total_sectors = pstat.totalSectors;
    ok.ext_flash_used_sectors = pstat.usedSectors;
    ok.ext_flash_free_sectors = pstat.freeSectors;
    ok.ext_flash_reserved_sectors = pstat.reservedSectors;
    ok.profile_count = pstat.profileCount;

    uint32_t count = 0;
    for (uint8_t i = 0; i < static_cast<uint8_t>(RegionId::Count); ++i) {
        const RegionId id = static_cast<RegionId>(i);
        if (id == RegionId::Flash) {
            continue;
        }
        const RegionUsage usage = region(id);
        ok.regions[count].size = usage.size;
        ok.regions[count].used = usage.used;
        ok.regions[count].reserved = usage.reserved;
        copyText(ok.regions[count].name, sizeof ok.regions[count].name,
                 regionName(id));
        ++count;
    }
    ok.region_count = count;
    ok.regions_count = static_cast<pb_size_t>(count);
}

} // namespace ThetaGP::Comm
