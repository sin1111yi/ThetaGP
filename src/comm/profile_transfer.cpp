/*
 * This file is a part of ThetaGP.
 */

#include "comm/profile_transfer.h"

#include <cstddef>

#include "comm/frame_codec.h"
#include "gamepad/profile/profile_store.h"
#include "pb_decode.h"
#include "pb_encode.h"

namespace ThetaGP::Comm {
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
static_assert(kPieceBytes + kFrameOverhead <= FrameCodec::PAYLOAD_MAX,
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

} // namespace ThetaGP::Comm
