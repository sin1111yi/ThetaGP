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
// reads a body out of the store's staging buffer, or writes one into it, is
// owed while a body's bytes are spoken for -- so the gate is answered once for
// the whole domain rather than by each arm, and an arm added to the dispatch
// is refused while that buffer is spoken for without carrying a call of its
// own. The two arms a staged write is carried by are the arms that staging
// exists for, so they are the ones the gate lets past while a staged write is
// what holds the buffer; the arm that opens a staged write is met ahead of the
// gate, where a staging the host never ended is dropped and the opening frame
// takes the buffer itself rather than being refused for what it arrived over;
// every other arm, the two carried ones included, is refused while a body is
// on its way back to the host, because the buffer a stream is read out of is
// the buffer a staged body lands in. The arms behind the gate are declared
// below it and reached only through it, which is how a body is read from that
// buffer by no arm but a refused one.
//
// The two arms the domain serves that read no body -- the store's own status
// record and its list of the ids it carries -- are answered from the store's
// index and reach no staging buffer, so no gate stands in front of them and
// they are answered whatever a body's bytes are doing.
class ProfileHandler {
public:
    // What answering an arm of this domain produced.
    enum class Answers {
        // The arm's own answer: a frame of the host's stream was answered.
        Reply,
        // The answer to the end of a staged write. It belongs to the exchange
        // the frames of the write made up and not to one frame of the host's,
        // so the number it carries is the zero the schema reserves for that --
        // on the body that was written and on the refusal alike.
        Exchange,
        // No answer at all: an arm whose success is answered with no frame,
        // because what it did is visible in the frame that ends the write.
        Nothing,
        // No arm of this domain, which leaves the refusal to the envelope that
        // routed the request here.
        NotMine,
    };

    // The arm the request names, answered into the reply. The request is the
    // message the frame was read into and the payload is the frame's own
    // bytes: the field a staged body's piece arrives in is a callback field,
    // and a callback field is collected out of the frame's bytes rather than
    // out of the message decoded from them.
    static Answers handle(const uint8_t *payload, uint16_t length,
                          const ThetaGP_Request &request,
                          ThetaGP_Reply &reply);

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

    // Begin a profile body: the length it will hold is what the request
    // declares, the store's id is assigned when the body is written, and the
    // frames that follow bring the body. An opening frame is answered with no
    // frame of its own -- there is nothing to report until there is a body.
    static Answers create(const ThetaGP_Request &request,
                          ThetaGP_Reply &reply);

    // One piece of the body being staged, taken out of the frame that brought
    // it and appended to the staging buffer. A taken piece is answered with no
    // frame at all; a refused one is answered with the refusal it is owed, and
    // the staging is void with it.
    static Answers chunk(const uint8_t *payload, uint16_t length,
                         const ThetaGP_Request &request,
                         ThetaGP_Reply &reply);

    // The end of a staged body: the bytes the staging buffer holds are written
    // to the store as one body, and the answer is the opening arm's own
    // success arm carrying the id the store assigned. A body short of the
    // length its opening frame declared is refused and never written.
    static void putEnd(ThetaGP_Reply &reply);

    // Drop a profile from the store: the store's id no longer answers, and the
    // body it held is not read back for anything but the write that replaced
    // it.
    static void remove(const ThetaGP_Request &request, ThetaGP_Reply &reply);

    // Make a profile the active one: the body the configuration the device
    // runs on is read from at the next boot.
    static void select(const ThetaGP_Request &request, ThetaGP_Reply &reply);
};

} // namespace ThetaGP::Comm
