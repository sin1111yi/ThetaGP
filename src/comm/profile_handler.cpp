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

} // namespace

bool ProfileHandler::handle(const ThetaGP_Request &request,
                            ThetaGP_Reply &reply) {
    // The two arms that read no body, answered ahead of the gate: they answer
    // from the store's index and touch nothing a stream or a staged write
    // holds, so there is nothing to refuse them for.
    switch (request.which_kind) {
    case ThetaGP_Request_profile_status_tag:
        status(reply);
        return true;
    case ThetaGP_Request_profile_list_tag:
        list(reply);
        return true;
    default:
        break;
    }

    // The gate, judged once for the domain and on the way in: every arm the
    // switch below answers reads a body, and every one of them reads it from
    // the store's one staging buffer. An arm added to that switch is refused
    // here while a stream or a staged write holds the buffer, without a call
    // of its own; an arm added above this point is an arm that reads no body,
    // which is the only kind the gate has nothing to say to. A request refused
    // here is refused whatever arm it names, and a host that asks again once
    // the stream or the staged write is done gets its answer.
    if (refusedWhileBodyBusy(reply)) {
        return true;
    }

    switch (request.which_kind) {
    case ThetaGP_Request_profile_get_tag:
        get(request, reply);
        return true;
    default:
        // No arm of this domain: the envelope that routed the request here
        // answers it as the command it has no unit for.
        return false;
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

} // namespace ThetaGP::Comm

#endif // THETAGP_CFG_HAS_FLASH
