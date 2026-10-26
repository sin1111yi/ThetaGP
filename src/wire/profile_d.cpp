/*
 * This file is a part of ThetaGP.
 */

#include "wire/profile_d.h"

#include <cstddef>

#include "pb_decode.h"
#include "pb_encode.h"
#include "wire/frame.h"
#include "wire/dispatch.h"
#include "conf/ThetaGP_Config.h"
#include "gamepad/config/config_manager.h"
#include "gamepad/profile/profile_store.h"
#include "utils/json/json.h"

namespace ThetaGP::Wire {
namespace {

// Body bytes one piece carries. A 4096 byte body is five pieces at this size,
// the last of them short.
constexpr uint16_t kPieceBytes = 1000;

// The frame around a piece at its widest: the number it carries, the arm it
// carries, and the piece's offset and length. The compile below holds a piece
// and the frame around it to one payload.
constexpr uint16_t kFrameOverhead = 20;
static_assert(kPieceBytes + kFrameOverhead <= Frame::PAYLOAD_MAX,
              "a piece and the frame around it must fit one payload");

// The most a body may be. A piece reaching past it is refused by its length,
// so no write of a piece lands outside the staging buffer.
constexpr uint32_t kStagedBodyMax = Gamepad::Profile::PROFILE_STAGING_SIZE - 1;

// ── the direction a body's bytes are spoken for in ──

// The one buffer the store reads a body into and writes a body out of, and
// which of the two holds it. The directions are exclusive.
enum class Direction : uint8_t { None, Read, Write };
Direction s_direction = Direction::None;

// ── the body on its way back to the host ──

// The stream's record: the id and length its opening frame reported, and the
// position the next piece starts at.
uint16_t s_id = 0;
uint16_t s_total = 0;
uint16_t s_offset = 0;
const uint8_t *s_body = nullptr;

// The piece the frame built last carries: its bytes, the length it was built
// for, and the bytes the encoder reported writing.
struct Piece {
    const uint8_t *data;
    uint16_t len;
    uint16_t written;
};
Piece s_piece = {nullptr, 0, 0};

// Write the piece's bytes out where the encoder asks for them, and record that
// they went: a piece whose bytes were never written is a frame the host reads
// as a whole piece of zero bytes.
bool writePiece(pb_ostream_t *stream, const pb_field_iter_t *field,
                void *const *arg) {
    const Piece *piece = static_cast<const Piece *>(*arg);
    if (!pb_encode_tag_for_field(stream, field)) {
        return false;
    }
    if (!pb_encode_string(stream, piece->data, piece->len)) {
        return false;
    }
    s_piece.written = piece->len;
    return true;
}

// ── the body on its way to the store ──

// The staged body's record: the opening frame it belongs to, the length that
// frame declared, and the bytes the staging buffer holds.
ProfileTransfer::Opening s_writeOpening{};
uint16_t s_writeTotal = 0;
uint16_t s_writeReceived = 0;

// The piece the frame last read carries: the length it declared and the bytes
// that landed in the staging buffer.
struct Chunk {
    uint32_t declared;
    uint32_t stored;
};
Chunk s_chunk = {0, 0};

// Takes the piece's bytes into the staging buffer, and reads past the ones it has
// no room for: the arm that answers holds the declared length against the stored
// one.
bool collectPiece(pb_istream_t *stream, const pb_field_iter_t *,
                  void **arg) {
    Chunk *chunk = static_cast<Chunk *>(*arg);
    const uint32_t declared = static_cast<uint32_t>(stream->bytes_left);
    const uint32_t room =
        kStagedBodyMax - (s_writeReceived + chunk->stored);
    const uint32_t take = (declared < room) ? declared : room;

    if (take != 0 &&
        !pb_read(stream,
                 Gamepad::Profile::s_staging + s_writeReceived + chunk->stored,
                 take)) {
        return false;
    }
    chunk->stored += take;
    chunk->declared += declared;

    if (stream->bytes_left != 0 &&
        !pb_read(stream, nullptr, stream->bytes_left)) {
        return false;
    }
    return true;
}

// The stream is over: nothing of it is filled in from here on, and the bytes it
// pointed at are the caller's again.
void closeBody() {
    s_direction = Direction::None;
    s_id = 0;
    s_total = 0;
    s_offset = 0;
    s_body = nullptr;
    s_piece.data = nullptr;
    s_piece.len = 0;
    s_piece.written = 0;
    s_writeOpening = ProfileTransfer::Opening{};
    s_writeTotal = 0;
    s_writeReceived = 0;
    s_chunk.declared = 0;
    s_chunk.stored = 0;
}

} // namespace

bool ProfileTransfer::busy() { return s_direction != Direction::None; }

bool ProfileTransfer::open(uint16_t id, const char *data, uint16_t len) {
    if (data == nullptr || s_direction != Direction::None) {
        // A stream opens nothing without bytes to read, and nothing while another
        // body's bytes are spoken for.
        return false;
    }
    s_id = id;
    s_total = len;
    s_offset = 0;
    s_body = reinterpret_cast<const uint8_t *>(data);
    s_piece.data = nullptr;
    s_piece.len = 0;
    s_piece.written = 0;
    s_direction = Direction::Read;
    return true;
}

bool ProfileTransfer::next(ThetaGP_Reply &reply) {
    if (s_direction != Direction::Read) {
        return false;
    }

    const uint16_t left = static_cast<uint16_t>(s_total - s_offset);
    if (left == 0) {
        // The last frame of the stream: what it carried and which body it was
        // read from, for a host to hold against the opening frame.
        reply.which_kind = ThetaGP_Reply_profile_end_tag;
        reply.kind.profile_end.total = s_total;
        reply.kind.profile_end.id = s_id;
        closeBody();
        return true;
    }

    const uint16_t piece = (left < kPieceBytes) ? left : kPieceBytes;

    // The piece's bytes are read from the body where it lies, so a host holds the
    // pieces against the length the opening frame gave it.
    s_piece.data = s_body + s_offset;
    s_piece.len = piece;
    s_piece.written = 0;

    reply.which_kind = ThetaGP_Reply_profile_chunk_tag;
    reply.kind.profile_chunk.offset = s_offset;
    reply.kind.profile_chunk.data.funcs.encode = writePiece;
    reply.kind.profile_chunk.data.arg = &s_piece;

    s_offset = static_cast<uint16_t>(s_offset + piece);
    return true;
}

bool ProfileTransfer::shortFrame() {
    return s_piece.written != s_piece.len;
}

bool ProfileTransfer::writeOpen() { return s_direction == Direction::Write; }

uint16_t ProfileTransfer::writeTotal() { return s_writeTotal; }

uint16_t ProfileTransfer::writeReceived() { return s_writeReceived; }

ProfileTransfer::Opening ProfileTransfer::opening() { return s_writeOpening; }

void ProfileTransfer::beginWrite(uint16_t total, Opening opening) {
    s_writeOpening = opening;
    s_writeTotal = total;
    s_writeReceived = 0;
    s_chunk.declared = 0;
    s_chunk.stored = 0;
    s_direction = Direction::Write;
}

ProfileTransfer::Piece ProfileTransfer::put(const uint8_t *payload,
                                            uint16_t length,
                                            const ThetaGP_Request &request) {
    if (s_direction != Direction::Write) {
        return Piece::NoStaging;
    }

    // The frame is read a second time, with the piece's collector installed on the
    // arm the message stands at. A copy is read, so the caller's reading of the
    // frame does not move.
    ThetaGP_Request again = request;
    again.kind.profile_put_chunk.data.funcs.decode = collectPiece;
    again.kind.profile_put_chunk.data.arg = &s_chunk;

    s_chunk.declared = 0;
    s_chunk.stored = 0;
    pb_istream_t in = pb_istream_from_buffer(payload, length);
    const bool read = pb_decode_noinit(&in, ThetaGP_Request_fields, &again);

    const uint32_t received = s_writeReceived;
    const uint32_t total = s_writeTotal;
    const uint32_t room = total - received;

    // The piece's length first: a piece no bytes could be taken from is not a
    // length the stream has room for.
    const bool took = read && s_chunk.stored != 0 &&
                      s_chunk.declared == s_chunk.stored &&
                      s_chunk.stored <= room;
    if (!took) {
        closeBody();
        return Piece::WrongLength;
    }

    // The offset is held against the stream and is not a place anything is written
    // at; a frame whose offset says otherwise is refused with the whole staging.
    if (again.kind.profile_put_chunk.offset != received) {
        closeBody();
        return Piece::WrongOffset;
    }

    s_writeReceived = static_cast<uint16_t>(received + s_chunk.stored);
    return Piece::Taken;
}

bool ProfileTransfer::commit(uint16_t *newId) {
    if (s_direction != Direction::Write) {
        return false;
    }

    const uint16_t total = s_writeTotal;
    const uint16_t received = s_writeReceived;
    // The profile the body is written to, decided by the frame that opened the
    // staging and read here because the staging is closed below.
    const bool factory = s_writeOpening.factory;
    const uint16_t named = s_writeOpening.id;

    // The staging holds a body only when the whole of it is there, and it is closed
    // before the store is asked.
    closeBody();
    if (received != total) {
        return false;
    }

    const char *body =
        reinterpret_cast<const char *>(Gamepad::Profile::s_staging);
    // The store reads the bytes out of the staging buffer, so this is their last
    // reader. A factory body replaces the one the device falls back to, an id
    // replaces that profile's own, and an arm naming none asks for one.
    uint16_t id = named;
    bool written = false;
    if (factory) {
        written = Gamepad::Profile::ProfileStore::getInstance()
                      .writeFactoryProfile(body, received);
    } else if (named != 0) {
        written = Gamepad::Profile::ProfileStore::getInstance()
                      .modifyProfile(named, body, received);
    } else {
        written = Gamepad::Profile::ProfileStore::getInstance()
                      .createProfile(body, received, &id);
    }
    if (written && newId != nullptr) {
        // The factory profile's id is the one the store writes no id for: it
        // is the fixed id that body is read back under.
        *newId = factory ? 0 : id;
    }
    return written;
}

void ProfileTransfer::abandon() { closeBody(); }
} // namespace ThetaGP::Wire

// On a board that carries no flash the store is not there, and these arms are
// answered with the unknown-command refusal.
#if THETAGP_CFG_HAS_FLASH

namespace ThetaGP::Wire {
namespace {

using Gamepad::Config::ConfigManager;
using Gamepad::Profile::PROFILE_JSON_MAX;
using Gamepad::Profile::PROFILE_MAX_ID;
using Gamepad::Profile::ProfileStatus;
using Gamepad::Profile::ProfileStore;
using Gamepad::Profile::ProfileText;

// The name a profile's body carries, written into the entry's field: the body
// is read here because the name travels in it, and a body that carries none
// leaves the field empty.
void readProfileName(ProfileStore &store, uint16_t id, char *out, size_t cap) {
    out[0] = '\0';
    ProfileText text;
    if (!store.readProfile(id, &text) || text.len == 0) {
        return;
    }
    Util::Json body;
    body.parse(text.data, static_cast<int>(text.len));
    (void)body.getStrCopy("name", out, static_cast<int>(cap));
}


// The list reply's entry capacity, read back from the schema's own max_count.
constexpr size_t kListCapacity =
    sizeof(ThetaGP_ProfileListOk::profiles) /
    sizeof(ThetaGP_ProfileListOk::profiles[0]);

// The list arm writes one entry per id the store carries, so the array holds
// room for one per id.
static_assert(static_cast<size_t>(PROFILE_MAX_ID) + 1 <= kListCapacity,
              "the reply's entries array must have room for every profile id");

// An id added to the store's range with no room left would write past the end,
// which is what this holds the two counts against.
bool carriesStagedWrite(pb_size_t arm) {
    return arm == ThetaGP_Request_profile_put_chunk_tag ||
           arm == ThetaGP_Request_profile_put_end_tag;
}

// The two arms a staged write is carried by, and the only ones the gate lets
// past while a staging holds the buffer.
bool opensStagedWrite(pb_size_t arm) {
    return arm == ThetaGP_Request_profile_create_tag ||
           arm == ThetaGP_Request_profile_start_tag;
}

} // namespace

ProfileDomain::Answers ProfileDomain::handle(const uint8_t *payload,
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

    // The two arms that open a staged write. They take the staging buffer and are
    // met ahead of the gate.
    if (opensStagedWrite(request.which_kind) && ProfileTransfer::writeOpen()) {
        ProfileTransfer::abandon();
    }

    // An opening frame that arrives over a staging drops it, and the staging it
    // opens counts its bytes from the first one.
    const bool carried =
        carriesStagedWrite(request.which_kind) && ProfileTransfer::writeOpen();
    if (!carried && refusedWhileBodyBusy(reply)) {
        // The refusal the end of a staged write is owed belongs to the
        // exchange the same way the answer to a body it wrote does.
        return request.which_kind == ThetaGP_Request_profile_put_end_tag
                   ? Answers::Exchange
                   : Answers::Reply;
    }

    // The answer the dispatch below produces.
    Answers answer = Answers::NotMine;
    switch (request.which_kind) {
    case ThetaGP_Request_profile_get_tag:
        get(request, reply);
        answer = Answers::Reply;
        break;
    case ThetaGP_Request_profile_create_tag:
        answer = create(request, reply);
        break;
    case ThetaGP_Request_profile_start_tag:
        answer = start(request, reply);
        break;
    case ThetaGP_Request_profile_put_chunk_tag:
        answer = chunk(payload, length, request, reply);
        break;
    case ThetaGP_Request_profile_put_end_tag:
        putEnd(reply);
        answer = Answers::Exchange;
        break;
    case ThetaGP_Request_profile_delete_tag:
        remove(request, reply);
        answer = Answers::Reply;
        break;
    case ThetaGP_Request_profile_select_tag:
        select(request, reply);
        answer = Answers::Reply;
        break;
    case ThetaGP_Request_profile_save_tag:
        save(reply);
        answer = Answers::Reply;
        break;
    case ThetaGP_Request_profile_load_tag:
        load(request, reply);
        answer = Answers::Reply;
        break;
    default:
        // No arm of this domain: the envelope that routed the request here
        // answers it as the command it has no unit for.
        break;
    }

    // The answer the arm above produced, handed back.
    return answer;
}

// The gate every arm that reaches the staging buffer passes: refused while a
// body's bytes are spoken for, in either direction.
bool ProfileDomain::refusedWhileBodyBusy(ThetaGP_Reply &reply) {
    if (!ProfileTransfer::busy()) {
        return false;
    }
    writeFailure(reply, ThetaGP_ErrorCode_ERR_BUSY, ThetaGP_Reason_REASON_NONE);
    return true;
}

void ProfileDomain::status(ThetaGP_Reply &reply) {
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

void ProfileDomain::list(ThetaGP_Reply &reply) {
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
    readProfileName(store, 0, ok.profiles[0].name, sizeof ok.profiles[0].name);

    // The code carries the whole refusal: the device holds bytes an arm would have
    // to write over, and a reason naming the same fact would be the code again.
    pb_size_t count = 1;
    for (uint32_t id = 1; id <= PROFILE_MAX_ID; ++id) {
        if (!store.carriesProfile(static_cast<uint16_t>(id))) {
            continue;
        }
        ok.profiles[count].id = id;
        ok.profiles[count].active = (id == state.activeId);
        readProfileName(store, static_cast<uint16_t>(id), ok.profiles[count].name,
                        sizeof ok.profiles[count].name);
        ++count;
    }
    ok.profiles_count = count;
}

void ProfileDomain::get(const ThetaGP_Request &request, ThetaGP_Reply &reply) {
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

    // Whether the store carries the id: the address ring holds it, and an id never
    // used or already deleted stays out. A profile's name is its body's, so an id
    // that exists is read once here for it.
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

    // An id the address map misses is a profile that is not there, which is not the
    // answer a failed read gives.
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

ProfileDomain::Answers ProfileDomain::create(const ThetaGP_Request &request,
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

    // The body is in the staging buffer, and the stream that carries it out holds it
    // until its closing frame has been built.
    ProfileTransfer::beginWrite(static_cast<uint16_t>(total),
                                {ThetaGP_Request_profile_create_tag, false, 0});
    return Answers::Nothing;
}

ProfileDomain::Answers ProfileDomain::start(const ThetaGP_Request &request,
                                              ThetaGP_Reply &reply) {
    const ThetaGP_ProfilePutStart &arm = request.kind.profile_start;

    // The body does not arrive with this frame: the staging opens over the length the
    // request declares, and the frames that follow bring it. Nothing is answered
    // here.
    if (arm.total == 0 || arm.total > PROFILE_JSON_MAX) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_VALUE_OUT_OF_RANGE, 1,
                     PROFILE_JSON_MAX);
        return Answers::Reply;
    }

    // A length no body may have is refused for its range rather than opened.
    if (arm.id > PROFILE_MAX_ID) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_ID_OUT_OF_RANGE, 0,
                     static_cast<uint32_t>(PROFILE_MAX_ID));
        return Answers::Reply;
    }

    ProfileTransfer::beginWrite(static_cast<uint16_t>(arm.total),
                                {ThetaGP_Request_profile_start_tag, arm.id == 0,
                                 static_cast<uint16_t>(arm.id)});
    return Answers::Nothing;
}

ProfileDomain::Answers ProfileDomain::chunk(const uint8_t *payload,
                                              uint16_t length,
                                              const ThetaGP_Request &request,
                                              ThetaGP_Reply &reply) {
    // The stream's readings before the piece is put: a refused piece voids the
    // staging, and the range a refusal reports is the one the piece arrived at.
    const uint32_t received = ProfileTransfer::writeReceived();
    const uint32_t total = ProfileTransfer::writeTotal();
    const uint32_t room = total - received;

    switch (ProfileTransfer::put(payload, length, request)) {
    case ProfileTransfer::Piece::Taken:
        // The piece is in the staging buffer. A piece is answered with no frame.
        return Answers::Nothing;
    case ProfileTransfer::Piece::NoStaging:
        // No staging is open, so the piece belongs to no body.
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_STATE,
                     ThetaGP_Reason_REASON_NONE);
        return Answers::Reply;
    case ProfileTransfer::Piece::WrongLength:
        // A piece may be as long as the body has left: an empty one and one that
        // runs past its end are the same refusal.
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_INVALID_LENGTH, 1, room);
        return Answers::Reply;
    case ProfileTransfer::Piece::WrongOffset:
        // The offset a frame reports is where its bytes were appended, so it has
        // to fall inside the body.
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_VALUE_OUT_OF_RANGE, received,
                     total - 1);
        return Answers::Reply;
    }
    return Answers::Reply;
}

void ProfileDomain::putEnd(ThetaGP_Reply &reply) {
    if (!ProfileTransfer::writeOpen()) {
        // The end of a body nothing opened: no body is written.
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_STATE,
                     ThetaGP_Reason_REASON_NONE);
        return;
    }

    const uint32_t received = ProfileTransfer::writeReceived();
    const uint32_t total = ProfileTransfer::writeTotal();
    // The opening frame, read before the write: the write closes the staging.
    const ProfileTransfer::Opening opening = ProfileTransfer::opening();

    // A body short of the length its opening frame declared is voided here.
    if (received != total) {
        ProfileTransfer::abandon();
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_INVALID_LENGTH, received, total);
        return;
    }

    uint16_t id = 0;
    if (!ProfileTransfer::commit(&id)) {
        // The store refused the write: a body meant for a new profile is one it
        // did not create, a body meant for the factory profile one it did not
        // write.
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INTERNAL,
                     opening.factory
                         ? ThetaGP_Reason_REASON_PROFILE_WRITE_FAILED
                         : ThetaGP_Reason_REASON_PROFILE_CREATE_FAILED);
        return;
    }

    // The body is a profile now: the opening arm's success arm carries the id it
    // is read back under and the length written.
    if (opening.arm == ThetaGP_Request_profile_create_tag) {
        reply.which_kind = ThetaGP_Reply_profile_create_tag;
        reply.kind.profile_create.id = id;
        reply.kind.profile_create.len = received;
        return;
    }
    reply.which_kind = ThetaGP_Reply_profile_start_tag;
    reply.kind.profile_start.id = id;
    reply.kind.profile_start.len = received;
}

void ProfileDomain::remove(const ThetaGP_Request &request,
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

void ProfileDomain::select(const ThetaGP_Request &request,
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

    if (!ConfigManager::getInstance().selectProfile(
            static_cast<uint16_t>(arm.id))) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INTERNAL,
                     ThetaGP_Reason_REASON_PROFILE_SELECT_FAILED);
        return;
    }

    reply.which_kind = ThetaGP_Reply_profile_select_tag;
    reply.kind.profile_select.id = arm.id;
}

void ProfileDomain::save(ThetaGP_Reply &reply) {
    ConfigManager &config = ConfigManager::getInstance();
    // The profile the configuration is written to, read once for the arm.
    const uint16_t active = config.activeProfileId();

    // No profile is written under the factory id: a save with it active is refused
    // for the state the device is in.
    if (active == 0) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_STATE,
                     ThetaGP_Reason_REASON_ACTIVE_PROFILE_IS_FACTORY);
        return;
    }

    // What the write did not carry over. A save that cannot say leaves it at zero.
    uint32_t droppedKeys = 0;
    if (!config.saveProfile(&droppedKeys)) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INTERNAL,
                     ThetaGP_Reason_REASON_PROFILE_SAVE_FAILED);
        return;
    }

    reply.which_kind = ThetaGP_Reply_profile_save_tag;
    ThetaGP_ProfileSaveOk &ok = reply.kind.profile_save;
    ok.id = active;
    // The count is a field of the answer only when it says something: a save
    // that carried every key over answers no count at all, and the field the
    // schema makes optional is the one that carries that difference.
    if (droppedKeys != 0) {
        ok.has_dropped_keys = true;
        ok.dropped_keys = droppedKeys;
    }
}

void ProfileDomain::load(const ThetaGP_Request &request,
                          ThetaGP_Reply &reply) {
    const ThetaGP_ProfileLoad &arm = request.kind.profile_load;
    ConfigManager &config = ConfigManager::getInstance();

    // A request naming no profile reads the active one, which is the profile the
    // configuration belongs to. A read is not a move onto it.
    uint32_t id = config.activeProfileId();
    if (arm.has_id) {
        // Naming a profile is not the same request as naming none, which is
        // what the field is optional for. An id beyond the range is refused as
        // the value it is, and not read as a request for the active one.
        if (arm.id > PROFILE_MAX_ID) {
            writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                         ThetaGP_Reason_REASON_ID_OUT_OF_RANGE, 0,
                         static_cast<uint32_t>(PROFILE_MAX_ID));
            return;
        }
        id = arm.id;
    }

    // The body is read into the running configuration, which the read resets to the
    // compiled defaults first. The active profile is not changed.
    if (!config.readProfileBody(static_cast<uint16_t>(id))) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INTERNAL,
                     ThetaGP_Reason_REASON_PROFILE_LOAD_FAILED);
        return;
    }

    reply.which_kind = ThetaGP_Reply_profile_load_tag;
    reply.kind.profile_load.id = id;
}

} // namespace ThetaGP::Wire

#endif // THETAGP_CFG_HAS_FLASH
