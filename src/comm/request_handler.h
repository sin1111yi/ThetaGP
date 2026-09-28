/*
 * This file is a part of ThetaGP.
 */

#pragma once

#include <cstdint>

#include "comm/frame_codec.h"

namespace ThetaGP::Comm {

// Reads one request payload and writes the payload of its answer. The reply
// carries the number of the request plus one, so a host pairs an answer with
// the frame it sent and sees which of its frames were never answered.
//
// A payload that does not read as a message is answered with a transport
// error, the same way the frame layer answers a frame it had to refuse. A
// command the unit does not answer is an error arm naming the command.
class RequestHandler {
public:
    static uint16_t answer(const uint8_t *payload, uint16_t length,
                           uint8_t *out, uint16_t capacity);

    // The answer to a frame the frame layer refused: the step that refused it,
    // and no number, because the frame was never read.
    static uint16_t frameRefused(FrameCodec::Drop drop, uint8_t *out,
                                 uint16_t capacity);
};

} // namespace ThetaGP::Comm
