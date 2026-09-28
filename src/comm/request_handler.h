/*
 * This file is a part of ThetaGP.
 */

#pragma once

#include <cstdint>

#include "comm/frame_codec.h"
#include "protocol/ThetaGP.pb.h"

namespace ThetaGP::Comm {

// The failure answer: the code names the class of refusal, the reason the fact
// in the request that refused it. One function builds it, so a refusal carries
// the same shape whichever arm answers it.
void writeFailure(ThetaGP_Reply &reply, ThetaGP_ErrorCode code,
                  ThetaGP_Reason reason);

// Reads one request payload and writes the payload of its answer. The reply
// carries the number of the request plus one, so a host pairs an answer with
// the frame it sent and sees which of its frames were never answered.
//
// A payload that does not read as a message is answered with a transport
// error, the same way the frame layer answers a frame it had to refuse. The
// arms of one domain are answered by that domain's own unit; an arm no unit
// answers is an error arm carrying ERR_UNKNOWN_CMD.
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
