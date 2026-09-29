/*
 * This file is a part of ThetaGP.
 */

#include "comm/flash_transfer.h"

#include <cstddef>

#include "build_info.h"
#include "comm/frame_codec.h"
#include "drivers/device/flash/flash_w25qxx.h"
#include "pb_encode.h"

namespace ThetaGP::Comm {
namespace {

using Drivers::Device::FlashW25qxx;

// The one staging buffer: the run of flash bytes the domain holds. A read keeps
// the whole run here while its pieces go out; a write collects its bytes here
// before they reach the chip. The two directions are exclusive, so one buffer
// holds whichever direction is under way.
COMMON_ZERO_INIT uint8_t s_stage[FlashTransfer::kStageBytes]{};

// The read stream's record: the address the run was read from, its length, and
// the position in it the next piece starts at. Nothing here is read while the
// stream is not open.
bool s_open = false;
uint32_t s_addr = 0;
uint16_t s_total = 0;
uint16_t s_offset = 0;

// The piece the frame built last carries: where its bytes are, how many the
// frame was built for, and how many the encoder reported writing.
struct Piece {
    const uint8_t *data;
    uint16_t len;
    uint16_t written;
};
Piece s_piece = {nullptr, 0, 0};

// The bytes one piece carries and the frame around it at its widest: the
// number the frame carries, the arm, the offset the piece was read at and the
// length of the piece. The bound is over the widths of the fields and not over
// the values a piece puts in them, so a field or a key widened moves the bound
// with it and this stops until the piece size moves too.
constexpr uint16_t kPieceBytes = 1000;
constexpr uint16_t kFrameOverhead = 20;
static_assert(kPieceBytes + kFrameOverhead <= FrameCodec::PAYLOAD_MAX,
              "a piece and the frame around it must fit one payload");

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

// The stream is over: nothing of it is filled in from here on, and the buffer
// it pointed into is the caller's to use again. This is the one place the
// record is cleared, so no ending can leave a part of it behind.
void closeRun() {
    s_open = false;
    s_addr = 0;
    s_total = 0;
    s_offset = 0;
    s_piece.data = nullptr;
    s_piece.len = 0;
    s_piece.written = 0;
}

} // namespace

bool FlashTransfer::active() { return s_open; }

uint8_t *FlashTransfer::staging() { return s_stage; }

bool FlashTransfer::open(uint32_t addr, uint32_t len) {
    if (s_open || len == 0 || len > kRunMax) {
        // Nothing is opened while the buffer is spoken for, and a length no run
        // may have is not a stream.
        return false;
    }
    if (!FlashW25qxx::getInstance().read(addr, s_stage, len)) {
        // The bytes are not there: a stream over a run that was not read would
        // announce a length the pieces could not fill.
        return false;
    }
    s_addr = addr;
    s_total = static_cast<uint16_t>(len);
    s_offset = 0;
    s_piece.data = nullptr;
    s_piece.len = 0;
    s_piece.written = 0;
    s_open = true;
    return true;
}

bool FlashTransfer::next(ThetaGP_Reply &reply) {
    if (!s_open) {
        return false;
    }

    const uint16_t left = static_cast<uint16_t>(s_total - s_offset);
    if (left == 0) {
        // The last frame of the stream: what it carried and the address it was
        // read from, for a host to hold against the opening frame.
        reply.which_kind = ThetaGP_Reply_test_flash_end_tag;
        reply.kind.test_flash_end.total = s_total;
        reply.kind.test_flash_end.addr = s_addr;
        closeRun();
        return true;
    }

    const uint16_t piece = (left < kPieceBytes) ? left : kPieceBytes;

    // The bytes of this piece are read from the run where it lies, and the
    // offset the frame reports is the position they were read at -- so a host
    // can hold the pieces against the length the opening frame gave it.
    s_piece.data = s_stage + s_offset;
    s_piece.len = piece;
    s_piece.written = 0;

    reply.which_kind = ThetaGP_Reply_test_flash_chunk_tag;
    reply.kind.test_flash_chunk.offset = s_offset;
    reply.kind.test_flash_chunk.data.funcs.encode = writePiece;
    reply.kind.test_flash_chunk.data.arg = &s_piece;

    s_offset = static_cast<uint16_t>(s_offset + piece);
    return true;
}

bool FlashTransfer::shortFrame() { return s_piece.written != s_piece.len; }

void FlashTransfer::abandon() { closeRun(); }

} // namespace ThetaGP::Comm
