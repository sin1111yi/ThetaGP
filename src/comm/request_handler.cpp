/*
 * This file is a part of ThetaGP.
 */

#include "comm/request_handler.h"

#include <cstring>

#include "comm/profile_handler.h"
#include "comm/sys_handler.h"
#include "conf/ThetaGP_Config.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "protocol/ThetaGP.pb.h"

namespace ThetaGP::Comm {

namespace {

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
            SysHandler::ping(reply);
            break;
        case ThetaGP_Request_sys_get_fw_version_tag:
            SysHandler::fwVersion(reply);
            break;
        case ThetaGP_Request_sys_reset_tag:
            SysHandler::reset(reply);
            break;
        case ThetaGP_Request_sys_enter_dfu_tag:
            SysHandler::enterDfu(reply);
            break;
        case ThetaGP_Request_sys_get_task_info_tag:
            SysHandler::taskInfo(request, reply);
            break;
        case ThetaGP_Request_sys_get_usage_tag:
            SysHandler::usage(reply);
            break;
#if THETAGP_CFG_HAS_FLASH
        case ThetaGP_Request_profile_status_tag:
            ProfileHandler::status(reply);
            break;
        case ThetaGP_Request_profile_list_tag:
            ProfileHandler::list(reply);
            break;
#endif
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

    // The one answer whose length follows the device's memory table: when it
    // does not fit a frame, the host is answered with the refusal instead of
    // nothing.
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
