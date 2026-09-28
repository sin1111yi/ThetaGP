/*
 * This file is a part of ThetaGP.
 */

#include "comm/request_handler.h"

#include <cstdio>
#include <cstring>

#include "conf/ThetaGP_Config.h"
#include "gamepad/profile/profile_store.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "protocol/ThetaGP.pb.h"
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

void writeFailure(ThetaGP_Reply &reply, ThetaGP_ErrorCode code,
                  ThetaGP_Reason reason) {
    reply.which_kind = ThetaGP_Reply_error_tag;
    reply.kind.error.has_code = true;
    reply.kind.error.code = code;
    reply.kind.error.reason = reason;
}

// The number a reply carries: the request's own number plus one. A host that
// writes no number gets the device's count instead, so the reply is numbered
// either way and a host still sees which frames went unanswered.
uint32_t nextNumber(const ThetaGP_Request &request) {
    static uint32_t last = 0;
    last = request.has_queued ? (request.queued + 1U) : (last + 1U);
    return last;
}

// What a refusal by the frame layer means on the wire.
ThetaGP_Reason reasonOf(Comm::FrameCodec::Drop drop) {
    switch (drop) {
    case Comm::FrameCodec::Drop::Checksum:
        return ThetaGP_Reason_REASON_FRAME_CHECKSUM;
    case Comm::FrameCodec::Drop::Prefix:
    case Comm::FrameCodec::Drop::Length:
        return ThetaGP_Reason_REASON_FRAME_PREFIX;
    case Comm::FrameCodec::Drop::Idle:
        return ThetaGP_Reason_REASON_FRAME_INCOMPLETE;
    case Comm::FrameCodec::Drop::NoRoom:
        return ThetaGP_Reason_REASON_FRAME_NO_ROOM;
    case Comm::FrameCodec::Drop::None:
    default:
        return ThetaGP_Reason_REASON_NONE;
    }
}

// ── sys.get_fw_version (command 2) ──

constexpr const char *kFirmwareVersion = "0.1.1";

void writeFwVersion(ThetaGP_Reply &reply) {
    reply.which_kind = ThetaGP_Reply_sys_get_fw_version_tag;
    ThetaGP_SysGetFwVersionOk &ok = reply.kind.sys_get_fw_version;
    copyText(ok.board, sizeof ok.board, BOARD_NAME);
    copyText(ok.version, sizeof ok.version, kFirmwareVersion);
    copyText(ok.build_date, sizeof ok.build_date, __DATE__);
    copyText(ok.build_time, sizeof ok.build_time, __TIME__);
}

// ── sys.reset (command 3) ──

void writeReset(ThetaGP_Reply &reply) {
    reply.which_kind = ThetaGP_Reply_sys_reset_tag;
}

// ── sys.enter_dfu (command 4) ──

void writeEnterDfu(ThetaGP_Reply &reply) {
    writeFailure(reply, ThetaGP_ErrorCode_ERR_NOT_SUPPORTED,
                 ThetaGP_Reason_REASON_NOT_IMPLEMENTED);
}

// ── sys.get_task_info (command 5) ──

void writeTaskInfo(const ThetaGP_Request &request, ThetaGP_Reply &reply) {
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

// ── sys.get_usage (command 6) ──

constexpr size_t kMaxRegions =
    static_cast<size_t>(Util::MemInfo::RegionId::Count);

void writeUsage(ThetaGP_Reply &reply) {
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

// ── the answer ──

uint16_t encodeReply(const ThetaGP_Reply &reply, uint8_t *out,
                     uint16_t capacity) {
    pb_ostream_t os = pb_ostream_from_buffer(out, capacity);
    if (!pb_encode(&os, ThetaGP_Reply_fields, &reply)) {
        return 0;
    }
    return static_cast<uint16_t>(os.bytes_written);
}

} // namespace

uint16_t RequestHandler::answer(const uint8_t *payload, uint16_t length,
                                uint8_t *out, uint16_t capacity) {
    if (out == nullptr || capacity == 0) {
        return 0;
    }

    ThetaGP_Request request;
    std::memset(&request, 0, sizeof request);
    pb_istream_t in = pb_istream_from_buffer(payload, length);
    const bool read = pb_decode_noinit(&in, ThetaGP_Request_fields, &request);

    ThetaGP_Reply reply;
    std::memset(&reply, 0, sizeof reply);

    if (!read) {
        // The bytes are not a message at all: the same shape the frame layer
        // answers a frame it had to refuse with, and no number, because there
        // is nothing to pair it with.
        reply.which_kind = ThetaGP_Reply_transport_error_tag;
        reply.kind.transport_error.reason =
            ThetaGP_Reason_REASON_PAYLOAD_UNREADABLE;
    } else {
        reply.has_queued = true;
        reply.queued = nextNumber(request);
        switch (request.which_kind) {
        case ThetaGP_Request_sys_ping_tag:
            reply.which_kind = ThetaGP_Reply_sys_ping_tag;
            break;
        case ThetaGP_Request_sys_get_fw_version_tag:
            writeFwVersion(reply);
            break;
        case ThetaGP_Request_sys_reset_tag:
            writeReset(reply);
            break;
        case ThetaGP_Request_sys_enter_dfu_tag:
            writeEnterDfu(reply);
            break;
        case ThetaGP_Request_sys_get_task_info_tag:
            writeTaskInfo(request, reply);
            break;
        case ThetaGP_Request_sys_get_usage_tag:
            writeUsage(reply);
            break;
        default:
            writeFailure(reply, ThetaGP_ErrorCode_ERR_UNKNOWN_CMD,
                         ThetaGP_Reason_REASON_UNKNOWN_COMMAND);
            break;
        }
    }

    const uint16_t written = encodeReply(reply, out, capacity);
    if (written != 0) {
        return written;
    }

    if (read && request.which_kind == ThetaGP_Request_sys_get_usage_tag) {
        const bool had_queued = reply.has_queued;
        const uint32_t count = reply.queued;
        std::memset(&reply, 0, sizeof reply);
        reply.has_queued = had_queued;
        reply.queued = count;
        writeFailure(reply, ThetaGP_ErrorCode_ERR_NOT_SUPPORTED,
                     ThetaGP_Reason_REASON_REPLY_TOO_LONG);
        return encodeReply(reply, out, capacity);
    }
    return 0;
}

uint16_t RequestHandler::frameRefused(Comm::FrameCodec::Drop drop,
                                     uint8_t *out, uint16_t capacity) {
    if (out == nullptr || capacity == 0) {
        return 0;
    }
    ThetaGP_Reply reply;
    std::memset(&reply, 0, sizeof reply);
    reply.which_kind = ThetaGP_Reply_transport_error_tag;
    reply.kind.transport_error.reason = reasonOf(drop);
    return encodeReply(reply, out, capacity);
}

} // namespace ThetaGP::Comm
