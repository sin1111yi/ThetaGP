/*
 * This file is a part of ThetaGP.
 */

#pragma once

#include "protocol/ThetaGP.pb.h"

namespace ThetaGP::Comm {

// The profile domain's read-only arms. Each function fills one reply with the
// answer its own arm carries and sets the kind that names that answer.
class ProfileHandler {
public:
    // The store's own status record: the active profile, the two ring
    // positions, and how the store's flash sectors are accounted for.
    static void status(ThetaGP_Reply &reply);

    // The profiles the store carries, the factory profile first and then
    // ascending id, each entry flagged with whether it is the active profile.
    // Existence comes from the store's address map, so the arm reads no body.
    static void list(ThetaGP_Reply &reply);
};

} // namespace ThetaGP::Comm
