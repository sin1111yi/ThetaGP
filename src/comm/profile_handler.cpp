/*
 * This file is a part of ThetaGP.
 */

#include "comm/profile_handler.h"

#include <cstddef>

#include "comm/profile_transfer.h"
#include "comm/reply_error.h"
#include "conf/ThetaGP_Config.h"
#include "gamepad/profile/profile_store.h"

// The store lives on an external flash chip, so on a board that carries none
// the arms reading it are not served at all: the switch in the request handler
// leaves them unhandled and the host is answered with the unknown-command
// refusal rather than with an account of a store that is not there.
#if THETAGP_CFG_HAS_FLASH

namespace ThetaGP::Comm {
namespace {

using Gamepad::Profile::PROFILE_JSON_MAX;
using Gamepad::Profile::PROFILE_MAX_ID;
using Gamepad::Profile::ProfileStatus;
using Gamepad::Profile::ProfileStore;
using Gamepad::Profile::ProfileText;

// The capacity of the list reply's entries array, as the generated message
// declares it. The number is not restated here: the schema's field options
// declare a max_count for that field, nanopb writes it as the array's own
// length, and this reads that declaration back.
constexpr size_t kListCapacity =
    sizeof(ThetaGP_ProfileListOk::profiles) /
    sizeof(ThetaGP_ProfileListOk::profiles[0]);

// The list arm writes one entry per id it carries, the factory profile's id
// always among them, so the array must have room for one entry per id. An id
// added to the store's range with no room left in the array would write past
// its end, and this is where the two counts are held against each other.
static_assert(static_cast<size_t>(PROFILE_MAX_ID) + 1 <= kListCapacity,
              "the reply's entries array must have room for every profile id");

// The two arms a staged write is carried by: the one that brings the body's
// pieces and the one that ends it. They are the arms a staging exists for, so
// they are the only ones the gate lets past while a staged write holds the
// store's staging buffer.
bool carriesStagedWrite(pb_size_t arm) {
    return arm == ThetaGP_Request_profile_put_chunk_tag ||
           arm == ThetaGP_Request_profile_put_end_tag;
}

// The arm a staged write is opened by: the frame that declares the body's
// length and takes the store's staging buffer for it. A staging speaks for
// three arms -- this one and the two above -- and this is the only one of the
// three a staging in hand leaves unheld: an opening frame is answered where it
// is met, before the gate, and what it meets there is a staging it drops
// rather than a device that refuses it.
bool opensStagedWrite(pb_size_t arm) {
    return arm == ThetaGP_Request_profile_create_tag;
}

} // namespace

ProfileHandler::Answers ProfileHandler::handle(const uint8_t *payload,
                                               uint16_t length,
                                               const ThetaGP_Request &request,
                                               ThetaGP_Reply &reply) {
    // The two arms that read no body, answered ahead of the gate: they answer
    // from the store's index and touch nothing a stream or a staged write
    // holds, so there is nothing to refuse them for.
    switch (request.which_kind) {
    case ThetaGP_Request_profile_status_tag:
        status(reply);
        return Answers::Reply;
    case ThetaGP_Request_profile_list_tag:
        list(reply);
        return Answers::Reply;
    default:
        break;
    }

    // An opening frame that arrives over a staged write drops the staging in
    // hand: the length it declared and the position it reached are forgotten
    // with it, and the staging the frame opens in its place counts its bytes
    // from the first one. A staging that never reached the arm that ends it
    // holds no body the store was ever written, so dropping one loses nothing
    // that could have been written -- and it is what a host is left with
    // instead of a timer: no staging is closed by the clock, and no staging is
    // a state the next opening frame cannot leave.
    if (opensStagedWrite(request.which_kind) && ProfileTransfer::writeOpen()) {
        ProfileTransfer::abandon();
    }

    // The gate, judged once for the domain and on the way in: every arm the
    // switch below answers reads a body out of the store's one staging buffer
    // or writes one into it. The arms a staged write is carried by are let
    // past only while a staged write is what holds the buffer -- a staged
    // write's own frames are the frames that staging exists for -- and the arm
    // that opens one is let past by the opening above, which has left it the
    // buffer already; it is refused here like any other arm while the buffer
    // holds a body on its way back to the host. Every other arm, those two
    // included, is refused while a body is on its way back, because the buffer
    // a stream is read out of is the buffer a staged body lands in. An arm
    // added to that switch is refused here without a call of its own; an arm
    // added above this point is an arm that reads no body, which is the only
    // kind the gate has nothing to say to. A request refused here is refused
    // whatever arm it names, and a host that asks again once the stream or the
    // staged write is done gets its answer.
    const bool carried =
        carriesStagedWrite(request.which_kind) && ProfileTransfer::writeOpen();
    if (!carried && refusedWhileBodyBusy(reply)) {
        // The refusal the end of a staged write is owed belongs to the
        // exchange the same way the answer to a body it wrote does.
        return request.which_kind == ThetaGP_Request_profile_put_end_tag
                   ? Answers::Exchange
                   : Answers::Reply;
    }

    switch (request.which_kind) {
    case ThetaGP_Request_profile_get_tag:
        get(request, reply);
        return Answers::Reply;
    case ThetaGP_Request_profile_create_tag:
        return create(request, reply);
    case ThetaGP_Request_profile_put_chunk_tag:
        return chunk(payload, length, request, reply);
    case ThetaGP_Request_profile_put_end_tag:
        putEnd(reply);
        return Answers::Exchange;
    case ThetaGP_Request_profile_delete_tag:
        remove(request, reply);
        return Answers::Reply;
    case ThetaGP_Request_profile_select_tag:
        select(request, reply);
        return Answers::Reply;
    default:
        // No arm of this domain: the envelope that routed the request here
        // answers it as the command it has no unit for.
        return Answers::NotMine;
    }
}

bool ProfileHandler::refusedWhileBodyBusy(ThetaGP_Reply &reply) {
    if (!ProfileTransfer::busy()) {
        return false;
    }

    // The code carries the whole of it: the device is holding bytes an arm
    // would have to write over, and a host that asks again once the stream or
    // the staged write is done gets its answer. No reason is added to it,
    // because a reason naming the same fact would be the code again.
    writeFailure(reply, ThetaGP_ErrorCode_ERR_BUSY, ThetaGP_Reason_REASON_NONE);
    return true;
}

void ProfileHandler::status(ThetaGP_Reply &reply) {
    const ProfileStatus state = ProfileStore::getInstance().getStatus();

    reply.which_kind = ThetaGP_Reply_profile_status_tag;
    ThetaGP_ProfileStatusOk &ok = reply.kind.profile_status;
    ok.active_profile_id = state.activeId;
    ok.profile_count = state.profileCount;
    ok.next_addr = state.nextAddr;
    ok.boot_meta_seq = state.bootMetaSeq;
    ok.address_ring_seq = state.addressRingSeq;
    ok.total_sectors = state.totalSectors;
    ok.used_sectors = state.usedSectors;
    ok.free_sectors = state.freeSectors;
    ok.reserved_sectors = state.reservedSectors;
}

void ProfileHandler::list(ThetaGP_Reply &reply) {
    ProfileStore &store = ProfileStore::getInstance();
    const ProfileStatus state = store.getStatus();

    reply.which_kind = ThetaGP_Reply_profile_list_tag;
    ThetaGP_ProfileListOk &ok = reply.kind.profile_list;
    ok.active = static_cast<uint32_t>(state.activeId);
    // The factory profile first, and unconditionally: its body sits at an
    // address of its own that the address ring takes no part in, so the store
    // carries its id whatever the ring holds.
    ok.profiles[0].id = 0;
    ok.profiles[0].active = (state.activeId == 0);

    // An entry stands for an id the store carries: the ids the address ring
    // holds, an id never used and one deleted staying out. Existence comes from
    // the store's own map of those addresses, which is answered without reading
    // a body -- so this arm reads no flash at all, and leaves whatever the
    // store's staging buffer held exactly as it found it.
    pb_size_t count = 1;
    for (uint32_t id = 1; id <= PROFILE_MAX_ID; ++id) {
        if (!store.carriesProfile(static_cast<uint16_t>(id))) {
            continue;
        }
        ok.profiles[count].id = id;
        ok.profiles[count].active = (id == state.activeId);
        ++count;
    }
    ok.profiles_count = count;
}

void ProfileHandler::get(const ThetaGP_Request &request, ThetaGP_Reply &reply) {
    // This arm reads a body, so the entry's gate has been past to reach it:
    // the read below lands on the store's staging buffer only when no stream
    // and no staged write holds it.
    const ThetaGP_ProfileGet &arm = request.kind.profile_get;
    if (!arm.has_id) {
        // A request that names no profile at all: the schema makes the field
        // optional so that this is tellable from a request naming id 0.
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_VALUE_MISSING);
        return;
    }
    if (arm.id > PROFILE_MAX_ID) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_ID_OUT_OF_RANGE, 0,
                     static_cast<uint32_t>(PROFILE_MAX_ID));
        return;
    }

    ProfileStore &store = ProfileStore::getInstance();
    const uint16_t id = static_cast<uint16_t>(arm.id);

    // Whether the store has an address for the id and whether the bytes at
    // that address came back are two different answers. The address map says
    // the first without reading the flash at all, so an id it holds no entry
    // for is the profile not being there rather than a read that failed.
    if (!store.carriesProfile(id)) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_PROFILE_NOT_FOUND);
        return;
    }

    ProfileText text;
    if (!store.readProfile(id, &text)) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INTERNAL,
                     ThetaGP_Reason_REASON_PROFILE_READ_FAILED);
        return;
    }
    if (text.len == 0) {
        // The store holds an address for the id and no body at it: what a
        // host is told is that there is no profile to read.
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_PROFILE_NOT_FOUND);
        return;
    }

    // The body is in the store's staging buffer now. The stream that carries
    // it out opens on those bytes and holds them until its closing frame has
    // been built, which is what the entry's gate refuses every other arm that
    // reads a body for in the meantime. A read that came back with a length
    // and no bytes to read at that length opens nothing, and is answered for
    // as what it is.
    if (!ProfileTransfer::open(id, text.data, text.len)) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INTERNAL,
                     ThetaGP_Reason_REASON_PROFILE_READ_FAILED);
        return;
    }

    reply.which_kind = ThetaGP_Reply_profile_get_tag;
    ThetaGP_ProfileGetStart &start = reply.kind.profile_get;
    start.total = text.len;
    start.id = id;
}

ProfileHandler::Answers ProfileHandler::create(const ThetaGP_Request &request,
                                               ThetaGP_Reply &reply) {
    const uint32_t total = request.kind.profile_create.total;

    // A body is what the store holds a profile in, so a stream that declares
    // none of it, or more of it than a body may hold, is refused for the range
    // the length must fall in rather than opened.
    if (total == 0 || total > PROFILE_JSON_MAX) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_VALUE_OUT_OF_RANGE, 1,
                     PROFILE_JSON_MAX);
        return Answers::Reply;
    }

    // The body does not arrive with this frame: the staging opens over the
    // length the request declares, and the frames that follow bring the body.
    // Nothing is answered here, because there is nothing to report until the
    // body has been written -- and the id it is written under is the store's
    // to assign when it is.
    ProfileTransfer::beginWrite(static_cast<uint16_t>(total));
    return Answers::Nothing;
}

ProfileHandler::Answers ProfileHandler::chunk(const uint8_t *payload,
                                              uint16_t length,
                                              const ThetaGP_Request &request,
                                              ThetaGP_Reply &reply) {
    // The stream's own readings, taken before the piece is put: a refused
    // piece voids the staging, and what the refusal names as the range the
    // piece fell outside of is the stream as it stood when the piece arrived.
    const uint32_t received = ProfileTransfer::writeReceived();
    const uint32_t total = ProfileTransfer::writeTotal();
    const uint32_t room = total - received;

    switch (ProfileTransfer::put(payload, length, request)) {
    case ProfileTransfer::Piece::Taken:
        // The piece is in the staging buffer, and there is nothing to say
        // about it: a piece is answered with no frame at all, so the host's
        // next frame follows it on the wire without a reply between them.
        return Answers::Nothing;
    case ProfileTransfer::Piece::NoStaging:
        // No body was opened, so there is no stream for a piece to belong to.
        // The code names the whole of it -- the state the device is in refused
        // the request -- and a reason names a fact the request carried, of
        // which a frame with no body open carries none.
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_STATE,
                     ThetaGP_Reason_REASON_NONE);
        return Answers::Reply;
    case ProfileTransfer::Piece::WrongLength:
        // What a piece may be is as long as the body has left and no longer:
        // an empty piece and one that runs past the body's end are the same
        // refusal, and both ends of the range are reported.
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_INVALID_LENGTH, 1, room);
        return Answers::Reply;
    case ProfileTransfer::Piece::WrongOffset:
        // The offset a frame reports is the position its bytes were appended
        // at, so the range it must fall in is the offsets the stream has room
        // for: the one it stood at, and the last one of the body.
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_VALUE_OUT_OF_RANGE, received,
                     total - 1);
        return Answers::Reply;
    }
    return Answers::Reply;
}

void ProfileHandler::putEnd(ThetaGP_Reply &reply) {
    if (!ProfileTransfer::writeOpen()) {
        // The end of a body nothing opened: no stream is brought to an end by
        // it, and no body is written. The code names the whole of it, and no
        // reason is added to a code that already says it.
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_STATE,
                     ThetaGP_Reason_REASON_NONE);
        return;
    }

    const uint32_t received = ProfileTransfer::writeReceived();
    const uint32_t total = ProfileTransfer::writeTotal();

    // The body is written only when the whole of it is there. A body short of
    // the length its opening frame declared is a piece of a body, and a piece
    // of a body is not what the store is asked for: the stream is voided and
    // the refusal reports what arrived against what was declared.
    if (received != total) {
        ProfileTransfer::abandon();
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_INVALID_LENGTH, received, total);
        return;
    }

    uint16_t id = 0;
    if (!ProfileTransfer::commit(&id)) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INTERNAL,
                     ThetaGP_Reason_REASON_PROFILE_CREATE_FAILED);
        return;
    }

    // The body is a profile now, and it is the arm that opened the staging
    // that says so: profile.create is the arm that opens one, so its own
    // success arm carries the id the store assigned and the length written.
    reply.which_kind = ThetaGP_Reply_profile_create_tag;
    reply.kind.profile_create.id = id;
    reply.kind.profile_create.len = received;
}

void ProfileHandler::remove(const ThetaGP_Request &request,
                            ThetaGP_Reply &reply) {
    const uint32_t id = request.kind.profile_delete.id;

    if (id > PROFILE_MAX_ID) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_ID_OUT_OF_RANGE, 0,
                     static_cast<uint32_t>(PROFILE_MAX_ID));
        return;
    }
    if (id == 0) {
        // The factory profile is the body the device falls back to, so it is
        // the one profile the store never drops: the request is refused for it
        // rather than answered with a write that failed.
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_CANNOT_DELETE_FACTORY, 1,
                     static_cast<uint32_t>(PROFILE_MAX_ID));
        return;
    }

    if (!ProfileStore::getInstance().deleteProfile(
            static_cast<uint16_t>(id))) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INTERNAL,
                     ThetaGP_Reason_REASON_PROFILE_DELETE_FAILED);
        return;
    }

    reply.which_kind = ThetaGP_Reply_profile_delete_tag;
    reply.kind.profile_delete.id = id;
}

void ProfileHandler::select(const ThetaGP_Request &request,
                            ThetaGP_Reply &reply) {
    const ThetaGP_ProfileSelect &arm = request.kind.profile_select;
    if (!arm.has_id) {
        // A request that names no profile at all: the schema makes the field
        // optional so that this is tellable from a request naming id 0, which
        // is the factory profile and a profile the store can be left on.
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_VALUE_MISSING);
        return;
    }
    if (arm.id > PROFILE_MAX_ID) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_ID_OUT_OF_RANGE, 0,
                     static_cast<uint32_t>(PROFILE_MAX_ID));
        return;
    }

    if (!ProfileStore::getInstance().selectProfile(
            static_cast<uint16_t>(arm.id))) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INTERNAL,
                     ThetaGP_Reason_REASON_PROFILE_SELECT_FAILED);
        return;
    }

    reply.which_kind = ThetaGP_Reply_profile_select_tag;
    reply.kind.profile_select.id = arm.id;
}

} // namespace ThetaGP::Comm

#endif // THETAGP_CFG_HAS_FLASH
