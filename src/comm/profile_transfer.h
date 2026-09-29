/*
 * This file is a part of ThetaGP.
 */

#pragma once

#include <cstdint>

#include "protocol/ThetaGP.pb.h"

namespace ThetaGP::Comm {

// The body stream of the profile domain: the one body on its way back to the
// host, held between the frames that carry it, and the one body on its way to
// the store, held between the frames that bring it. One frame is built per
// call, because the wire carries one frame at a time.
//
// The two directions are exclusive, because the store reads a body into one
// buffer and writes one out of it. The bytes of a body on their way back to
// the host stay in that buffer until the frame that closes the stream is
// built; the bytes of a body on their way to the store are appended to that
// same buffer as the frames bringing them arrive, and are written out of it
// when the frame that ends the body has been answered. Whichever of the two is
// under way, the buffer is spoken for, which is what an arm that reads or
// writes a body is refused for while it is.
class ProfileTransfer {
public:
    // Whether a body's bytes are spoken for: by a body on its way back to the
    // host, or by a body being staged for the store. An arm that asks is
    // refused the same way whichever of the two is holding.
    static bool busy();

    // ── the body on its way back to the host ──

    // Open a stream over the body of `id`: `len` bytes starting at `data`,
    // which stay where the caller put them until the stream's closing frame
    // has been built. Reports whether a stream is open: a length with no bytes
    // to read it from opens nothing, so the caller can answer for a body it
    // cannot send rather than announcing one it has not got. A stream is
    // opened over the one buffer the store reads a body into, so nothing is
    // opened while another body's bytes are spoken for.
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

    // ── the body on its way to the store ──

    // What the frame that opened a staging said about the write it begins: the
    // arm it was, and whether it named the factory profile. The two travel
    // together because one frame set both, and both outlive the frame -- the
    // arm decides which of the opening arms' success arms answers the end of
    // the write, and the profile it named decides the store call the body is
    // written by.
    struct Opening {
        // The arm the staging was opened by, as the request's own kind.
        pb_size_t arm = 0;
        // Whether that arm named the factory profile: profile.start with id 0.
        // An arm with no id of its own never names it, and every other opening
        // asks the store for a user profile.
        bool factory = false;
    };

    // Whether a body is being staged for the store: the bytes it has received
    // are in the store's staging buffer, and the frames that follow the one
    // that opened the staging append to them.
    static bool writeOpen();

    // The length the opening frame declared for the staged body, and how many
    // of its bytes the staging buffer holds.
    static uint16_t writeTotal();
    static uint16_t writeReceived();

    // Open a staging for a body of `total` bytes, to be written to the profile
    // the opening frame names. Nothing of the body reaches the store until it
    // is written, and where it is written is that frame's to say: a body for a
    // new profile is written under the id the store assigns when it is, and
    // the factory profile is the one id the store does not assign.
    static void beginWrite(uint16_t total, Opening opening);

    // The opening frame the staging in hand belongs to: what that frame said
    // of how the body is answered and of the profile it is written to. It is
    // read before the write, because writing a body closes the staging.
    static Opening opening();

    // What one piece of a staged body came to.
    enum class Piece {
        // The piece's bytes are in the staging buffer and the stream stands
        // past them.
        Taken,
        // No body is being staged: there is nothing to append to.
        NoStaging,
        // The piece is not a length the stream has room for: it carried no
        // bytes at all, or more bytes than the body has left.
        WrongLength,
        // The piece does not start where the stream stands.
        WrongOffset,
    };

    // Take the piece this frame carries, appended to the staged body where the
    // stream stands. The piece is read out of the frame's own bytes and not
    // out of the message decoded from them: the field that carries it is a
    // callback field, and a callback field whose collector is missing is
    // dropped without a word, so what the frame carried is held against what
    // the staging took rather than against whether the read reported a
    // failure. A frame that carries no piece the staging may take, and a piece
    // that does not start where the stream stands, are refused for what they
    // are and void the staging: the pieces of a body that did not arrive whole
    // are not a body, and nothing of a refused stream is written.
    static Piece put(const uint8_t *payload, uint16_t length,
                     const ThetaGP_Request &request);

    // Write the staged body to the store as one body: the bytes in the staging
    // buffer are the body, and the staging is closed. A body for a new profile
    // is written under the id the store assigns, which is reported back; a
    // body for the factory profile goes to the address that body sits at and
    // reports the fixed id it is read under. A body short of the length its
    // opening frame declared is not written at all; the staging is closed
    // either way, because the stream it belonged to is over once the frame
    // that ends it has been answered. Reports whether the store took the body,
    // and the id it is read back under.
    static bool commit(uint16_t *newId);

    // Drop whatever holds the buffer -- an open stream or a staged body -- and
    // answer it no further: what the frames that would have followed it must
    // not do is carry pieces of a body whose opening frame never went out, and
    // what the store must not be handed is a body that was refused on the way
    // in.
    static void abandon();
};

} // namespace ThetaGP::Comm
