/*
 * This file is a part of ThetaGP.
 */

#pragma once

#include <cstdint>

#include "protocol/ThetaGP.pb.h"

namespace ThetaGP::Comm {

// The test domain's flash staging: the one run of flash bytes the domain holds
// between frames. A read keeps the whole run there while the pieces of it are
// sent out; a write collects its bytes there before they reach the chip. The
// two directions are exclusive, because they are the two uses of the one
// buffer, so a run on its way to the host and a run on its way to the chip are
// never held at once.
//
// One frame is built per call, because the wire carries one frame at a time.
class FlashTransfer {
public:
    // The bytes the one staging buffer holds, and the widest run it can carry:
    // the longest flash read the domain serves.
    static constexpr uint16_t kStageBytes = 4096;
    static constexpr uint32_t kRunMax = kStageBytes;

    // Whether a read stream is open: the staging buffer holds a run on its way
    // to the host until the frame that closes the stream has been built.
    static bool active();

    // Open a read stream over `len` bytes starting at `addr`: the run is read
    // into the staging buffer whole, and the frames that follow carry it out.
    // Reports whether a run of that length could be read; a read that failed
    // opens nothing, and nothing is opened while the buffer is spoken for.
    static bool open(uint32_t addr, uint32_t len);

    // Fill `reply` with the next frame of the open stream: a piece of the run
    // while bytes are left, the frame that closes the stream once they are all
    // in one, and nothing at all when no stream is open. The closing frame is
    // the last one, and the buffer is free again after it.
    static bool next(ThetaGP_Reply &reply);

    // Whether the frame built last carries fewer body bytes than it was built
    // to carry: the length of a piece is not among the fields the frame
    // carries, so the sender holds its own writer to the count it asked for.
    static bool shortFrame();

    // Drop whatever holds the buffer and answer it no further.
    static void abandon();

    // The one staging buffer and its capacity. A flash write collects its bytes
    // here, because a write and a read stream never hold the buffer at once.
    static uint8_t *staging();
};

} // namespace ThetaGP::Comm
