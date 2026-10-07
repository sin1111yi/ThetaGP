/*
 * This file is a part of ThetaGP.
 */

#include "wire/dispatch.h"

#include <cstring>

#include "wire/flash.h"
#include "wire/config_d.h"
#include "wire/profile_d.h"
#include "wire/sys_d.h"
#include "wire/test_d.h"
#include "conf/ThetaGP_Config.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "pb/ThetaGP.pb.h"

namespace ThetaGP::Wire {

void writeFailure(ThetaGP_Reply &reply, ThetaGP_ErrorCode code,
                  ThetaGP_Reason reason, uint32_t param1, uint32_t param2) {
    reply.which_kind = ThetaGP_Reply_error_tag;
    reply.kind.error.has_code = true;
    reply.kind.error.code = code;
    reply.kind.error.reason = reason;
    reply.kind.error.param1 = param1;
    reply.kind.error.param2 = param2;
}

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
ThetaGP_Reason reasonOf(Wire::Frame::Drop drop) {
    switch (drop) {
    case Wire::Frame::Drop::Checksum:
        return ThetaGP_Reason_REASON_FRAME_CHECKSUM;
    case Wire::Frame::Drop::Prefix:
    case Wire::Frame::Drop::Length:
        return ThetaGP_Reason_REASON_FRAME_PREFIX;
    case Wire::Frame::Drop::Idle:
        return ThetaGP_Reason_REASON_FRAME_INCOMPLETE;
    case Wire::Frame::Drop::NoRoom:
        return ThetaGP_Reason_REASON_FRAME_NO_ROOM;
    case Wire::Frame::Drop::None:
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

uint16_t Dispatch::answer(const uint8_t *payload, uint16_t length,
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
            SysDomain::ping(reply);
            break;
        case ThetaGP_Request_sys_get_fw_version_tag:
            SysDomain::fwVersion(reply);
            break;
        case ThetaGP_Request_sys_reset_tag:
            SysDomain::reset(reply);
            break;
        case ThetaGP_Request_sys_enter_dfu_tag:
            SysDomain::enterDfu(reply);
            break;
        case ThetaGP_Request_sys_get_task_info_tag:
            SysDomain::taskInfo(request, reply);
            break;
        case ThetaGP_Request_config_get_key_tag:
            ConfigDomain::getKey(request, reply);
            break;
        case ThetaGP_Request_config_set_key_tag:
            ConfigDomain::setKey(request, reply);
            break;
        case ThetaGP_Request_config_list_keys_tag:
            ConfigDomain::listKeys(reply);
            break;
        case ThetaGP_Request_sys_get_usage_tag:
            SysDomain::usage(reply);
            break;
#if THETAGP_CFG_HAS_FLASH
        case ThetaGP_Request_profile_status_tag:
        case ThetaGP_Request_profile_list_tag:
        case ThetaGP_Request_profile_get_tag:
        case ThetaGP_Request_profile_create_tag:
        case ThetaGP_Request_profile_start_tag:
        case ThetaGP_Request_profile_put_chunk_tag:
        case ThetaGP_Request_profile_put_end_tag:
        case ThetaGP_Request_profile_delete_tag:
        case ThetaGP_Request_profile_select_tag:
        case ThetaGP_Request_profile_save_tag:
        case ThetaGP_Request_profile_load_tag:
            // A flash read stream is served one frame per command tick, and the
            // buffer it holds is the buffer the flash test arms write into: a
            // profile arm that reads or writes a body waits for the stream to
            // end. The two arms that read no body answer from the store's index
            // and touch no staging buffer, so the stream leaves them alone.
            if (Flash::active() &&
                request.which_kind != ThetaGP_Request_profile_status_tag &&
                request.which_kind != ThetaGP_Request_profile_list_tag) {
                writeFailure(reply, ThetaGP_ErrorCode_ERR_BUSY,
                             ThetaGP_Reason_REASON_NONE);
                break;
            }
            // The profile domain's one entry, whatever arm the request names:
            // the dispatch to the arm and the refusal owed to every arm that
            // reads or writes a body while a body's bytes are spoken for both
            // live there, judged once on the way in. No arm of the domain
            // carries that refusal itself, so an arm added to the domain is
            // routed here the same way and is refused with the rest. The
            // frame's own bytes go with the request, because the field a
            // staged body's piece arrives in is a callback field and is
            // collected out of those bytes.
            switch (ProfileDomain::handle(payload, length, request, reply)) {
            case ProfileDomain::Answers::Reply:
                break;
            case ProfileDomain::Answers::Exchange:
                // The answer to the end of a staged write belongs to the
                // exchange the write made up and not to one frame of the
                // host's stream: the schema reserves the zero it carries for
                // exactly that, so a host does not pair it with a frame.
                reply.queued = 0;
                break;
            case ProfileDomain::Answers::Nothing:
                // An arm whose success is answered with no frame at all: the
                // piece is in the staging buffer, and there is nothing to
                // report about it until the body it belongs to is written.
                return 0;
            case ProfileDomain::Answers::NotMine:
                writeFailure(reply, ThetaGP_ErrorCode_ERR_UNKNOWN_CMD,
                             ThetaGP_Reason_REASON_UNKNOWN_COMMAND);
                break;
            }
            break;
#endif
#if THETAGP_CFG_HAS_FLASH
        case ThetaGP_Request_test_chip_erase_tag:
        case ThetaGP_Request_test_flash_info_tag:
        case ThetaGP_Request_test_spi_mode_tag:
        case ThetaGP_Request_test_erase_sector_tag:
        case ThetaGP_Request_test_compaction_tag:
        case ThetaGP_Request_test_flash_read_tag:
        case ThetaGP_Request_test_flash_write_tag:
            // The test domain's flash arms: every one of them reads the chip or
            // changes it, and every one of them shares the one flash staging
            // buffer. A stream already open is a frame of that buffer waiting
            // to go out, so the arms that would change the chip under it wait
            // for it to end; only the arm that opens a read stream is judged
            // below, against both domains' streams.
            if (request.which_kind != ThetaGP_Request_test_flash_read_tag &&
                Flash::active()) {
                writeFailure(reply, ThetaGP_ErrorCode_ERR_BUSY,
                             ThetaGP_Reason_REASON_NONE);
                break;
            }
            switch (request.which_kind) {
            case ThetaGP_Request_test_chip_erase_tag:
                TestDomain::chipErase(reply);
                break;
            case ThetaGP_Request_test_flash_info_tag:
                TestDomain::flashInfo(reply);
                break;
            case ThetaGP_Request_test_spi_mode_tag:
                TestDomain::spiMode(request, reply);
                break;
            case ThetaGP_Request_test_erase_sector_tag:
                TestDomain::eraseSector(request, reply);
                break;
            case ThetaGP_Request_test_compaction_tag:
                TestDomain::compaction(reply);
                break;
            case ThetaGP_Request_test_flash_read_tag:
                // A read stream is opened only while nothing else holds a
                // stream: a second one would leave two streams wanting the one
                // frame a tick can send.
                if (ProfileTransfer::busy() || Flash::active()) {
                    writeFailure(reply, ThetaGP_ErrorCode_ERR_BUSY,
                                 ThetaGP_Reason_REASON_NONE);
                    break;
                }
                TestDomain::flashRead(request, reply);
                break;
            case ThetaGP_Request_test_flash_write_tag:
                TestDomain::flashWrite(payload, length, request, reply);
                break;
            default:
                break;
            }
            break;
#endif
        case ThetaGP_Request_test_mem_info_tag:
            TestDomain::memInfo(reply);
            break;
        case ThetaGP_Request_test_keypad_scan_tag:
            TestDomain::keypadScan(reply);
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

uint16_t Dispatch::pending(uint8_t *out, uint16_t capacity) {
    if (out == nullptr || capacity == 0) {
        return 0;
    }

    ThetaGP_Reply reply;
    std::memset(&reply, 0, sizeof reply);

    // At most one stream is open, and the two never are at once: a flash read
    // stream answers from the test domain's staging buffer, a profile read
    // stream from the store's. Whichever is open is the one the tick serves.
    const bool flash = Flash::active();
    const bool built = flash ? Flash::next(reply)
                             : ProfileTransfer::next(reply);
    if (!built) {
        return 0;
    }

    // The frames of a stream answer the exchange and not one frame of the
    // host's, so they carry the zero the envelope reserves for that -- the
    // same number a staged write's own answer carries.
    reply.has_queued = true;
    reply.queued = 0;

    const uint16_t written = encodeReply(reply, out, capacity);
    if (written != 0 && !(flash ? Flash::shortFrame()
                                : ProfileTransfer::shortFrame())) {
        return written;
    }

    // The frame did not fit what the caller offered, or it does not carry the
    // body bytes it was built for. The length of a piece is not among the
    // fields the frame carries, so a host cannot tell a short piece from a
    // whole one: it is told the answer is too long instead, and the stream
    // that would have carried the rest ends with this frame.
    ProfileTransfer::abandon();
    Flash::abandon();
    return tooLongReply(reply, ThetaGP_ErrorCode_ERR_BUFFER_OVERFLOW, out,
                        capacity);
}

uint16_t Dispatch::frameRefused(Wire::Frame::Drop drop,
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

} // namespace ThetaGP::Wire
