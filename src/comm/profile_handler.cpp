/*
 * This file is a part of ThetaGP.
 */

#include "comm/profile_handler.h"

#include <cstddef>

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

} // namespace ThetaGP::Comm

#endif // THETAGP_CFG_HAS_FLASH
