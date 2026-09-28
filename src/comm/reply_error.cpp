/*
 * This file is a part of ThetaGP.
 */

#include "comm/reply_error.h"

namespace ThetaGP::Comm {

void writeFailure(ThetaGP_Reply &reply, ThetaGP_ErrorCode code,
                  ThetaGP_Reason reason) {
    reply.which_kind = ThetaGP_Reply_error_tag;
    reply.kind.error.has_code = true;
    reply.kind.error.code = code;
    reply.kind.error.reason = reason;
}

} // namespace ThetaGP::Comm
