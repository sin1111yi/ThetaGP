/*
 * This file is a part of ThetaGP.
 */

#include "comm/request_handler.h"

#include <cstring>

#include "comm/profile_handler.h"
#include "comm/profile_transfer.h"
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

// The refusal that stands in for an answer too wide for the frame the caller
// offered: the number the answer would have carried, and the reason both of
// the arms whose answers are not one fixed width are answered with.
uint16_t tooLongReply(const ThetaGP_Reply &answered, ThetaGP_ErrorCode code,
                      uint8_t *out, uint16_t capacity) {
    ThetaGP_Reply reply;
    std::memset(&reply, 0, sizeof reply);
    reply.has_queued = answered.has_queued;
    reply.queued = answered.queued;
    writeFailure(reply, code, ThetaGP_Reason_REASON_REPLY_TOO_LONG);
    return encodeReply(reply, out, capacity);
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
        case ThetaGP_Request_profile_list_tag:
        case ThetaGP_Request_profile_get_tag:
        case ThetaGP_Request_profile_create_tag:
        case ThetaGP_Request_profile_put_chunk_tag:
        case ThetaGP_Request_profile_put_end_tag:
        case ThetaGP_Request_profile_delete_tag:
        case ThetaGP_Request_profile_select_tag:
            // The profile domain's one entry, whatever arm the request names:
            // the dispatch to the arm and the refusal owed to every arm that
            // reads or writes a body while a body's bytes are spoken for both
            // live there, judged once on the way in. No arm of the domain
            // carries that refusal itself, so an arm added to the domain is
            // routed here the same way and is refused with the rest. The
            // frame's own bytes go with the request, because the field a
            // staged body's piece arrives in is a callback field and is
            // collected out of those bytes.
            switch (ProfileHandler::handle(payload, length, request, reply)) {
            case ProfileHandler::Answers::Reply:
                break;
            case ProfileHandler::Answers::Exchange:
                // The answer to the end of a staged write belongs to the
                // exchange the write made up and not to one frame of the
                // host's stream: the schema reserves the zero it carries for
                // exactly that, so a host does not pair it with a frame.
                reply.queued = 0;
                break;
            case ProfileHandler::Answers::Nothing:
                // An arm whose success is answered with no frame at all: the
                // piece is in the staging buffer, and there is nothing to
                // report about it until the body it belongs to is written.
                return 0;
            case ProfileHandler::Answers::NotMine:
                writeFailure(reply, ThetaGP_ErrorCode_ERR_UNKNOWN_CMD,
                             ThetaGP_Reason_REASON_UNKNOWN_COMMAND);
                break;
            }
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

    // The answers whose length follows the data in the device: when one does
    // not fit the frame the caller offered, the host is answered with the
    // refusal instead of nothing.
    if (!read) {
        return 0;
    }
    if (request.which_kind == ThetaGP_Request_sys_get_usage_tag) {
        return tooLongReply(reply, ThetaGP_ErrorCode_ERR_NOT_SUPPORTED, out,
                            capacity);
    }
    if (request.which_kind == ThetaGP_Request_profile_get_tag) {
        // The frames that would have carried the body follow the opening frame
        // this answer did not become: they must not go out for a body no host
        // was told the length of.
        ProfileTransfer::abandon();
        return tooLongReply(reply, ThetaGP_ErrorCode_ERR_BUFFER_OVERFLOW, out,
                            capacity);
    }
    return 0;
}

uint16_t RequestHandler::pending(uint8_t *out, uint16_t capacity) {
    if (out == nullptr || capacity == 0) {
        return 0;
    }

    ThetaGP_Reply reply;
    std::memset(&reply, 0, sizeof reply);
    if (!ProfileTransfer::next(reply)) {
        return 0;
    }

    // The frames of a stream answer the exchange and not one frame of the
    // host's, so they carry the zero the envelope reserves for that -- the
    // same number a staged write's own answer carries.
    reply.has_queued = true;
    reply.queued = 0;

    const uint16_t written = encodeReply(reply, out, capacity);
    if (written != 0 && !ProfileTransfer::shortFrame()) {
        return written;
    }

    // The frame did not fit what the caller offered, or it does not carry the
    // body bytes it was built for. The length of a piece is not among the
    // fields the frame carries, so a host cannot tell a short piece from a
    // whole one: it is told the answer is too long instead, and the stream
    // that would have carried the rest ends with this frame.
    ProfileTransfer::abandon();
    return tooLongReply(reply, ThetaGP_ErrorCode_ERR_BUFFER_OVERFLOW, out,
                        capacity);
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
