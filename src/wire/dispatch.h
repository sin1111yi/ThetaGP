/*
 * This file is a part of ThetaGP.
 */

#pragma once

#include <cstdint>

#include "wire/frame.h"
#include "pb/ThetaGP.pb.h"

namespace ThetaGP::Wire {

// The refusal: the code names the class, the reason the fact in the request that
// refused it, and the two numbers some reasons carry -- the ends of the range a
// value fell outside of. A reason that carries none leaves both at zero. One
// function builds it, so every arm answers with the same shape.
void writeFailure(ThetaGP_Reply &reply, ThetaGP_ErrorCode code,
                  ThetaGP_Reason reason, uint32_t param1 = 0,
                  uint32_t param2 = 0);

// Reads one request payload and writes the payload of its answer. The reply carries
// the request's number plus one, so a host pairs an answer with the frame it sent.
//
// A payload that does not read as a message is answered with a transport error. An
// arm no unit answers is an error arm carrying ERR_UNKNOWN_CMD.
class Dispatch {
public:
    static uint16_t answer(const uint8_t *payload, uint16_t length,
                           uint8_t *out, uint16_t capacity);

    // The answer to a frame the frame layer refused: the step that refused it,
    // and no number, because the frame was never read.
    static uint16_t frameRefused(Frame::Drop drop, uint8_t *out,
                                 uint16_t capacity);

    // The next frame of a stream a domain left open, and 0 when none is open.
    // A body sent back is more than one frame is wide, so the arm that opened
    // the stream answers with its first frame and the frames that carry the
    // body itself follow from here, one per call -- the wire carries one frame
    // at a time whichever frame it is. Those frames answer the exchange the
    // first one began rather than any frame of the host's, which is what the
    // zero they carry says.
    static uint16_t pending(uint8_t *out, uint16_t capacity);
};

} // namespace ThetaGP::Wire
