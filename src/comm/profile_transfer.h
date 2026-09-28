/*
 * This file is a part of ThetaGP.
 */

#pragma once

#include <cstdint>

#include "protocol/ThetaGP.pb.h"

namespace ThetaGP::Comm {

// The body stream of the profile domain: the one body on its way back, held
// between the frames that carry it. One frame is built per call, because the
// wire carries one frame at a time.
//
// The bytes themselves are not held here. The store reads a body into the
// staging buffer it keeps, and this holds only how far that read has been sent
// -- the id and the length the opening frame reports, and where the next piece
// starts. The buffer is spoken for from the read the stream was opened on
// until the frame that closes it is built, which is what an arm that reads or
// writes a body is refused for while this holds.
class ProfileTransfer {
public:
    // Whether a body's bytes are spoken for: today, by a body on its way back
    // to the host. A body being staged for a write is the other holder this
    // answers for, and it opens the same state here, so an arm that asks is
    // refused the same way whichever of the two is holding.
    static bool busy();

    // Open a stream over the body of `id`: `len` bytes starting at `data`,
    // which stay where the caller put them until the stream's closing frame
    // has been built. Reports whether a stream is open: a length with no bytes
    // to read it from opens nothing, so the caller can answer for a body it
    // cannot send rather than announcing one it has not got.
    static bool open(uint16_t id, const char *data, uint16_t len);

    // Fill `reply` with the next frame of the open stream: a piece of the body
    // while bytes are left, the frame that closes the stream once they are all
    // in one, and nothing at all when no stream is open. Reports whether a
    // frame was built. The closing frame is the last one, and the bytes are
    // free again after it.
    static bool next(ThetaGP_Reply &reply);

    // Whether the frame built last carries fewer body bytes than it was built
    // to carry. The length of a piece is not among the fields the frame
    // carries, so the sender holds its own writer to the count it asked for: a
    // writer the encoder never reached leaves the two apart, and a piece that
    // does not carry its bytes is a piece no host can tell from a whole one.
    static bool shortFrame();

    // Drop the open stream, if any, and answer it no further: what the frames
    // that would have followed it must not do is carry pieces of a body whose
    // opening frame never went out.
    static void abandon();
};

} // namespace ThetaGP::Comm
