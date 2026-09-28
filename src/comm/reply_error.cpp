/*
 * This file is a part of ThetaGP.
 */

#include "comm/reply_error.h"

namespace ThetaGP::Comm {

void writeFailure(ThetaGP_Reply &reply, ThetaGP_ErrorCode code,
                  ThetaGP_Reason reason, uint32_t param1, uint32_t param2) {
    reply.which_kind = ThetaGP_Reply_error_tag;
    reply.kind.error.has_code = true;
    reply.kind.error.code = code;
    reply.kind.error.reason = reason;
    reply.kind.error.param1 = param1;
    reply.kind.error.param2 = param2;
}

} // namespace ThetaGP::Comm
