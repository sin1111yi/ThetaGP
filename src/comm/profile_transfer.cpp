/*
 * This file is a part of ThetaGP.
 */

#include "comm/profile_transfer.h"

#include <cstddef>

#include "comm/frame_codec.h"
#include "gamepad/profile/profile_store.h"
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

// ── the stream ──

// The stream's own record: whether one is open, the id and the length its
// opening frame reported, and the position in the body the next piece starts
// at. The body is not copied anywhere: it stays in the store's staging buffer,
// which is why that buffer is spoken for while this record says so.
bool s_open = false;
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

// The stream is over, whichever way it ended: nothing of it is filled in from
// here on, and the bytes it pointed at are the caller's to use again.
void closeStream() {
    s_open = false;
    s_id = 0;
    s_total = 0;
    s_offset = 0;
    s_body = nullptr;
    s_piece.data = nullptr;
    s_piece.len = 0;
    s_piece.written = 0;
}

} // namespace

bool ProfileTransfer::busy() { return s_open; }

bool ProfileTransfer::open(uint16_t id, const char *data, uint16_t len) {
    if (data == nullptr) {
        closeStream();
        return false;
    }
    s_id = id;
    s_total = len;
    s_offset = 0;
    s_body = reinterpret_cast<const uint8_t *>(data);
    s_piece.data = nullptr;
    s_piece.len = 0;
    s_piece.written = 0;
    s_open = true;
    return true;
}

bool ProfileTransfer::next(ThetaGP_Reply &reply) {
    if (!s_open) {
        return false;
    }

    const uint16_t left = static_cast<uint16_t>(s_total - s_offset);
    if (left == 0) {
        // The last frame of the stream: what it carried and which body it was
        // read from, for a host to hold against the opening frame.
        reply.which_kind = ThetaGP_Reply_profile_end_tag;
        reply.kind.profile_end.total = s_total;
        reply.kind.profile_end.id = s_id;
        closeStream();
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

void ProfileTransfer::abandon() { closeStream(); }

} // namespace ThetaGP::Comm
