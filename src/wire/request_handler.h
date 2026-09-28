/*
 * This file is a part of ThetaGP.
 */

#pragma once

#include <cstdint>

namespace ThetaGP::Test {

// Turns one request payload into the payload of its answer: the command is the
// arm the envelope names, the answer carries the same queued count it was
// handed, and a command this unit does not answer is ERR_UNKNOWN_CMD. Framing,
// including the checksum, belongs to the frame layer.
class RequestHandler {
public:
    static uint16_t answer(const uint8_t *payload, uint16_t length,
                           uint32_t queued, uint8_t *out, uint16_t capacity);
};

} // namespace ThetaGP::Test
