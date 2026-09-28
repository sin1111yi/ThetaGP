/*
 * This file is a part of ThetaGP.
 */

#pragma once

#include "protocol/ThetaGP.pb.h"

namespace ThetaGP::Comm {

// The profile domain: one entry for the arms the domain serves, and the
// dispatch from there to the arm the request names.
//
// The entry is where the domain's gate is judged -- the refusal every arm that
// reads a body is owed while a stream or a staged write holds the store's
// staging buffer -- so the gate is answered once for the whole domain rather
// than by each arm, and an arm added to the dispatch is refused while that
// buffer is spoken for without carrying a call of its own. The arms behind the
// gate are declared below it and reached only through it, which is how a body
// is read from that buffer by no arm but a refused one.
//
// The two arms the domain serves that read no body -- the store's own status
// record and its list of the ids it carries -- are answered from the store's
// index and reach no staging buffer, so no gate stands in front of them and
// they are answered whatever a body's bytes are doing.
class ProfileHandler {
public:
    // The arm the request names, answered into the reply; false when the
    // request names no arm of this domain, which leaves the refusal to the
    // envelope that routed it here.
    static bool handle(const ThetaGP_Request &request, ThetaGP_Reply &reply);

    // The store's own status record: the active profile, the two ring
    // positions, and how the store's flash sectors are accounted for.
    static void status(ThetaGP_Reply &reply);

    // The profiles the store carries, the factory profile first and then
    // ascending id, each entry flagged with whether it is the active profile.
    // Existence comes from the store's address map, so the arm reads no body.
    static void list(ThetaGP_Reply &reply);

private:
    // The refusal an arm that reads a body is owed while a body's bytes are
    // spoken for, and whether it answered it. Such an arm starts here: the
    // store reads a body into one staging buffer and writes one out of it, so
    // an arm reaching that buffer while a stream or a staged write holds it
    // would answer from bytes that are not the ones it asked for.
    static bool refusedWhileBodyBusy(ThetaGP_Reply &reply);

    // One profile's body, sent as the opening frame of a stream: how long the
    // body is and which profile it was read from. The pieces of the body and
    // the frame that closes the stream follow from the transfer unit, one
    // frame at a time, because the wire carries one frame at a time.
    static void get(const ThetaGP_Request &request, ThetaGP_Reply &reply);
};

} // namespace ThetaGP::Comm
