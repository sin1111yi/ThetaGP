/*
 * This file is a part of ThetaGP.
 */

#pragma once

#include <cstdint>

#include "protocol/ThetaGP.pb.h"

namespace ThetaGP::Comm {

// The refusal: the code names the class of refusal, the reason the fact in the
// request that refused it, and the two numbers some reasons carry -- the ends
// of the range a value fell outside of, for the reasons the schema annotates
// that way. A reason that carries no numbers leaves both at zero, which is
// what a number of zero encodes as. One function builds it, so a refusal
// carries the same shape whichever arm answers it — and it sits in its own
// unit rather than in one of them, because every unit that answers a request
// answers refusals too, and one of them reaching into another for it would tie
// the two together.
void writeFailure(ThetaGP_Reply &reply, ThetaGP_ErrorCode code,
                  ThetaGP_Reason reason, uint32_t param1 = 0,
                  uint32_t param2 = 0);

} // namespace ThetaGP::Comm
