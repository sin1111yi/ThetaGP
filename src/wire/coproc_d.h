/*
 * This file is a part of ThetaGP.
 */

#pragma once

#include "pb/ThetaGP.pb.h"

namespace ThetaGP::Wire {

// The coprocessor domain: what the panel board is showing, and one navigation
// step for it. The panel's own messages belong to Link.proto; this is the
// host's window onto them.
class CoprocDomain {
public:
    // The panel's last word, as this board keeps it.
    static void state(ThetaGP_Reply &reply);

    // One step for the panel. The reply carries the page and the place the
    // panel last reported: the step is answered by the panel's next beat, so a
    // host that wants to see the move asks for the state once more.
    static void nav(const ThetaGP_Request &request, ThetaGP_Reply &reply);
};

} // namespace ThetaGP::Wire
