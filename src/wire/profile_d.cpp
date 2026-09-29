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

namespace ThetaGP::Wire {
namespace {

// Body bytes one piece carries. A 4096 byte body is five pieces at this size,
// the last of them short.
constexpr uint16_t kPieceBytes = 1000;

// The frame around a piece, at its widest: the number the frame carries, the
// arm that carries the piece, and the two fields inside it -- the offset the
// piece was read at and the length of the piece. A value at the widest the
// schema declares for its type takes five bytes of varint, and the key in
// front of it takes up to two, so the frame is at most twenty bytes wider
// than the body it carries -- the bound an offset and a number at their
// widest were measured at. The bound is over the widths of the fields and not
// over the values a piece puts in them, so a field or a key widened moves the
// bound with it and the compile below stops until the piece size moves too:
// the assert is what says a piece and the frame around it fit one payload,
// and it holds at the compile rather than at the encoder, where the answer
// would be a reply that never went out.
constexpr uint16_t kFrameOverhead = 20;
static_assert(kPieceBytes + kFrameOverhead <= Frame::PAYLOAD_MAX,
              "a piece and the frame around it must fit one payload");

// The most a body may be: the store's staging buffer holds one more byte than
// the largest body, and the terminator is no part of any body. A piece that
// would reach past this is refused by its length, so no write of a piece ever
// lands outside that buffer.
constexpr uint32_t kStagedBodyMax = Gamepad::Profile::PROFILE_STAGING_SIZE - 1;

// ── the direction a body's bytes are spoken for in ──

// The one buffer the store reads a body into and writes a body out of, and the
// one way its bytes can be held: by a body on its way back to the host, or by
// a body being staged for the store. The two are exclusive -- a stream over a
// body that is being written over is a stream over bytes the store is putting
// there -- so one record says which of them holds the buffer, and none of the
// fields below are read while it says another.
enum class Direction : uint8_t { None, Read, Write };
Direction s_direction = Direction::None;

// ── the body on its way back to the host ──

// The stream's own record: the id and the length its opening frame reported,
// and the position in the body the next piece starts at. The body is not
// copied anywhere: it stays in the store's staging buffer, which is why that
// buffer is spoken for while this record says so.
uint16_t s_id = 0;
uint16_t s_total = 0;
uint16_t s_offset = 0;
const uint8_t *s_body = nullptr;

// The piece the frame built last carries: where its bytes are, how many of
// them the frame was built for, and how many the encoder reported writing.
// The count of written bytes is what the sender holds the frame against, and
// the writer below is the only thing that moves it.
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

// The staged body's own record: the opening frame it belongs to -- the arm
// that opened it and the profile that frame named -- the length it declared,
// and how many of those bytes the staging buffer holds. The bytes themselves
// are not copied anywhere: they are appended to the store's staging buffer as
// the frames bringing them arrive, and that buffer is what the store writes
// out of when the body is written.
ProfileTransfer::Opening s_writeOpening{};
uint16_t s_writeTotal = 0;
uint16_t s_writeReceived = 0;

// The piece the frame last read carries: how many bytes the frame declared for
// it and how many of those landed in the staging buffer. The collector below
// is the only thing that moves either, and the two are apart exactly when the
// piece was longer than the room the staging buffer had left.
struct Chunk {
    uint32_t declared;
    uint32_t stored;
};
Chunk s_chunk = {0, 0};

// Append the piece's bytes to the staged body, from the position the stream
// stands at. The bytes land in the order they arrive and nothing of the frame
// decides where they land: the offset a frame reports is held against the
// stream's own position after the frame has been read, and a piece that does
// not start where the stream stands is voided with the whole staging. The
// bytes the staging has no room for are read out of the frame all the same and
// dropped -- an over-long piece is refused by its length, and nothing of a
// refused stream is written, so those bytes have no body to belong to.
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

// The stream is over, whichever way it ended and whichever direction it ran
// in: nothing of it is filled in from here on, and the bytes it pointed at are
// the caller's to use again. This is the one place the record is cleared, so
// no ending can leave a part of it behind.
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
        // A length with no bytes to read it from opens nothing, and neither
        // does a stream over one body while another body's bytes are spoken
        // for: the buffer a stream is read out of is the buffer a staged body
        // lands in, so the bytes in hand are not dropped for a stream that
        // cannot be opened.
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

    // The bytes of this piece are read from the body where it lies, and the
    // offset the frame reports is the position they were read at -- so a host
    // can hold the pieces against the length the opening frame gave it, and
    // the body never has to fit anywhere but the buffer it was read into.
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

    // The frame is read a second time, with the piece's collector installed on
    // the arm the message stands at. The message stands at that arm because
    // the read that filled it is the one that named it, and an arm the message
    // already stands at is not cleared before it is decoded, while an arm the
    // message does not stand at is -- so the collector survives the read that
    // collects with it, and a collector installed on an arm the message is not
    // standing at would be taken away before it was ever called. A copy of the
    // message is what is read, so nothing of the caller's own reading of the
    // frame moves.
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

    // The piece's own length first: a piece from which no bytes could be taken
    // is not a length the stream has room for, whether the frame could not be
    // read a second time, carried no bytes at all, or carried more of them
    // than the body has left. Both ends of the length such a piece must fall
    // in are reported to the arm answering for it.
    const bool took = read && s_chunk.stored != 0 &&
                      s_chunk.declared == s_chunk.stored &&
                      s_chunk.stored <= room;
    if (!took) {
        closeBody();
        return Piece::WrongLength;
    }

    // The offset the frame reports is a value held against the stream and not
    // a place anything is written at: the bytes were appended where the stream
    // stood, and a frame whose offset says otherwise is refused with the whole
    // staging.
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

    // The body is what the staging holds only when the whole of it is there: a
    // body short of the length its opening frame declared is a piece of a
    // body, and a piece of a body is not what the store is asked for. The
    // staging is closed before the store is asked, so the stream is over
    // whichever answer comes back.
    closeBody();
    if (received != total) {
        return false;
    }

    const char *body =
        reinterpret_cast<const char *>(Gamepad::Profile::s_staging);
    // The store reads the bytes out of the staging buffer, which is why they
    // had to be spoken for until this call: this write is the last reader of
    // them. A body for the factory profile replaces the one the device falls
    // back to and is written at the address that body sits at; every other
    // body asks for a profile of its own, and the id it lands under is the
    // store's to assign.
    uint16_t id = 0;
    const bool written = factory
                             ? Gamepad::Profile::ProfileStore::getInstance()
                                   .writeFactoryProfile(body, received)
                             : Gamepad::Profile::ProfileStore::getInstance()
                                   .createProfile(body, received, &id);
    if (written && newId != nullptr) {
        // The factory profile's id is the one the store writes no id for: it
        // is the fixed id that body is read back under.
        *newId = factory ? 0 : id;
    }
    return written;
}

void ProfileTransfer::abandon() { closeBody(); }
} // namespace ThetaGP::Wire

// The store lives on an external flash chip, so on a board that carries none
// the arms reading it are not served at all: the switch in the request handler
// leaves them unhandled and the host is answered with the unknown-command
// refusal rather than with an account of a store that is not there.
#if THETAGP_CFG_HAS_FLASH

namespace ThetaGP::Wire {
namespace {

using Gamepad::Config::ConfigManager;
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

// The arms a staged write is opened by: the frames that declare a body's
// length and take the store's staging buffer for it. A staging speaks for
// these two and the two that carry it, and they are the ones a staging in hand
// leaves unheld: an opening frame is answered where it is met, before the
// gate, and what it meets there is a staging it drops rather than a device
// that refuses it.
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
    // write's own frames are the frames that staging exists for -- and the
    // arms that open one are let past by the opening above, which has left
    // them the buffer already; they are refused here like any other arm while
    // the buffer holds a body on its way back to the host. Every other arm,
    // those two included, is refused while a body is on its way back, because
    // the buffer a stream is read out of is the buffer a staged body lands in.
    // An arm added to that switch is refused here without a call of its own; an
    // arm added above this point is an arm that reads no body, which is the
    // only kind the gate has nothing to say to. A request refused here is
    // refused whatever arm it names, and a host that asks again once the stream
    // or the staged write is done gets its answer.
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

bool ProfileDomain::refusedWhileBodyBusy(ThetaGP_Reply &reply) {
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

    // The body does not arrive with this frame: the staging opens over the
    // length the request declares, and the frames that follow bring the body.
    // Nothing is answered here, because there is nothing to report until the
    // body has been written -- and the id it is written under is the store's
    // to assign when it is.
    ProfileTransfer::beginWrite(static_cast<uint16_t>(total),
                                {ThetaGP_Request_profile_create_tag, false});
    return Answers::Nothing;
}

ProfileDomain::Answers ProfileDomain::start(const ThetaGP_Request &request,
                                              ThetaGP_Reply &reply) {
    const ThetaGP_ProfilePutStart &arm = request.kind.profile_start;

    // A body is what the store holds a profile in, so a stream that declares
    // none of it, or more of it than a body may hold, is refused for the range
    // the length must fall in rather than opened. The length is judged first,
    // because it is the number every frame that follows is held against
    // whatever profile the body is for.
    if (arm.total == 0 || arm.total > PROFILE_JSON_MAX) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_VALUE_OUT_OF_RANGE, 1,
                     PROFILE_JSON_MAX);
        return Answers::Reply;
    }

    // The profile the body is for. The factory profile's id is the one id the
    // store does not assign, so a request naming it is the request that
    // replaces the body the device falls back to; every other id in range asks
    // for a user profile, whose id the store assigns when the body is written.
    // The two are one kind of request either way, so what tells them apart is
    // the id alone.
    if (arm.id > PROFILE_MAX_ID) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_ID_OUT_OF_RANGE, 0,
                     static_cast<uint32_t>(PROFILE_MAX_ID));
        return Answers::Reply;
    }

    ProfileTransfer::beginWrite(static_cast<uint16_t>(arm.total),
                                {ThetaGP_Request_profile_start_tag,
                                 arm.id == 0});
    return Answers::Nothing;
}

ProfileDomain::Answers ProfileDomain::chunk(const uint8_t *payload,
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

void ProfileDomain::putEnd(ThetaGP_Reply &reply) {
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
    // The opening frame the staging belongs to, read before the write: writing
    // the body closes the staging, and it is the arm that opened it that
    // reports it.
    const ProfileTransfer::Opening opening = ProfileTransfer::opening();

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
        // The store refused the write, and what it refused is the arm that
        // opened the staging names: a body meant for a new profile is a
        // profile the store did not create, while a body meant for the factory
        // profile is a body it did not write -- which is what a device whose
        // fallback body is already there answers, the factory profile being
        // the one profile written only onto a flash that carries none.
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INTERNAL,
                     opening.factory
                         ? ThetaGP_Reason_REASON_PROFILE_WRITE_FAILED
                         : ThetaGP_Reason_REASON_PROFILE_CREATE_FAILED);
        return;
    }

    // The body is a profile now, and it is the arm that opened the staging
    // that says so: the opening arm's own success arm carries the id the body
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

    if (!ProfileStore::getInstance().selectProfile(
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
    // The profile the configuration is written to, read once for the whole
    // arm: the layer answers this from the store's status record, so it is the
    // profile the device is on, and the arm is refused on that profile rather
    // than answered with a write under another id.
    const uint16_t active = config.activeProfileId();

    // The factory profile is the body the device falls back to, and no profile
    // is written under its id: a save with it active is refused for the state
    // the device is in rather than answered with a write that failed. A request
    // naming the active profile is not what this arm takes -- it writes the one
    // the configuration is on -- so the refusal is the state and not a
    // parameter.
    if (active == 0) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_STATE,
                     ThetaGP_Reason_REASON_ACTIVE_PROFILE_IS_FACTORY);
        return;
    }

    // What the write did not carry over from the body it replaced. The count is
    // raised only by a write that reached the store, so a save that cannot say
    // what it dropped leaves it at zero and answers no count rather than a
    // count of none.
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

    // A request naming no profile reads the active one, and the active one is
    // the profile the configuration the device runs on belongs to -- the
    // profile the arm above writes it back to. A read is a read of a body and
    // not a move onto it: the active profile is where the arm found it,
    // whichever id the request carried, and the profile layer is handed that id
    // to read from rather than to become.
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

    // The body is read into the configuration the device runs on, which is
    // reset to the compiled defaults by the read itself: a body this firmware
    // cannot read leaves the device on those defaults and is answered as a
    // read that failed rather than as one that happened. The profile read is
    // the one the request named and not a move onto it, so the profile the
    // configuration belongs to after the read is the one it belonged to before
    // it.
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
