/*
 * This file is a part of ThetaGP.
 */

#pragma once

#include <cstdint>

#include "pb/ThetaGP.pb.h"

namespace ThetaGP::Wire {

// The body stream of the profile domain: the one body on its way back to the
// host, held between the frames that carry it, and the one body on its way to
// the store, held between the frames that bring it. One frame is built per
// call, because the wire carries one frame at a time.
//
// The two directions are exclusive, because the store reads a body into one
// buffer and writes one out of it. The bytes of a body on their way back to
// the host stay in that buffer until the frame that closes the stream is
// built; the bytes of a body on their way to the store are appended to that
// same buffer as the frames bringing them arrive, and are written out of it
// when the frame that ends the body has been answered. Whichever of the two is
// under way, the buffer is spoken for, which is what an arm that reads or
// writes a body is refused for while it is.
class ProfileTransfer {
public:
    // Whether a body's bytes are spoken for: by a body on its way back to the
    // host, or by a body being staged for the store. An arm that asks is
    // refused the same way whichever of the two is holding.
    static bool busy();

    // ── the body on its way back to the host ──

    // Open a stream over the body of `id`: `len` bytes starting at `data`,
    // which stay where the caller put them until the stream's closing frame
    // has been built. Reports whether a stream is open: a length with no bytes
    // to read it from opens nothing, so the caller can answer for a body it
    // cannot send rather than announcing one it has not got. A stream is
    // opened over the one buffer the store reads a body into, so nothing is
    // opened while another body's bytes are spoken for.
    static bool open(uint16_t id, const char *data, uint16_t len);

    // Fill `reply` with the next frame of the open stream: a piece of the body
    // while bytes are left, the frame that closes the stream once they are all
    // in one, and nothing at all when no stream is open. Reports whether a
    // frame was built. The closing frame is the last one, and the bytes are
    // free again after it.
    static bool next(ThetaGP_Reply &reply);

    // Whether the frame built last carries fewer body bytes than it was built
    // to carry. The length of a piece is not among the fields the frame
    // carries, so the sender holds its own writer to the count it asked for: a
    // writer the encoder never reached leaves the two apart, and a piece that
    // does not carry its bytes is a piece no host can tell from a whole one.
    static bool shortFrame();

    // ── the body on its way to the store ──

    // What the frame that opened a staging said about the write it begins: the
    // arm it was, and whether it named the factory profile. The two travel
    // together because one frame set both, and both outlive the frame -- the
    // arm decides which of the opening arms' success arms answers the end of
    // the write, and the profile it named decides the store call the body is
    // written by.
    struct Opening {
        // The arm the staging was opened by, as the request's own kind.
        pb_size_t arm = 0;
        // Whether that arm named the factory profile: profile.start with id 0.
        // An arm with no id of its own never names it, and every other opening
        // asks the store for a user profile.
        bool factory = false;
    };

    // Whether a body is being staged for the store: the bytes it has received
    // are in the store's staging buffer, and the frames that follow the one
    // that opened the staging append to them.
    static bool writeOpen();

    // The length the opening frame declared for the staged body, and how many
    // of its bytes the staging buffer holds.
    static uint16_t writeTotal();
    static uint16_t writeReceived();

    // Open a staging for a body of `total` bytes, to be written to the profile
    // the opening frame names. Nothing of the body reaches the store until it
    // is written, and where it is written is that frame's to say: a body for a
    // new profile is written under the id the store assigns when it is, and
    // the factory profile is the one id the store does not assign.
    static void beginWrite(uint16_t total, Opening opening);

    // The opening frame the staging in hand belongs to: what that frame said
    // of how the body is answered and of the profile it is written to. It is
    // read before the write, because writing a body closes the staging.
    static Opening opening();

    // What one piece of a staged body came to.
    enum class Piece {
        // The piece's bytes are in the staging buffer and the stream stands
        // past them.
        Taken,
        // No body is being staged: there is nothing to append to.
        NoStaging,
        // The piece is not a length the stream has room for: it carried no
        // bytes at all, or more bytes than the body has left.
        WrongLength,
        // The piece does not start where the stream stands.
        WrongOffset,
    };

    // Take the piece this frame carries, appended to the staged body where the
    // stream stands. The piece is read out of the frame's own bytes and not
    // out of the message decoded from them: the field that carries it is a
    // callback field, and a callback field whose collector is missing is
    // dropped without a word, so what the frame carried is held against what
    // the staging took rather than against whether the read reported a
    // failure. A frame that carries no piece the staging may take, and a piece
    // that does not start where the stream stands, are refused for what they
    // are and void the staging: the pieces of a body that did not arrive whole
    // are not a body, and nothing of a refused stream is written.
    static Piece put(const uint8_t *payload, uint16_t length,
                     const ThetaGP_Request &request);

    // Write the staged body to the store as one body: the bytes in the staging
    // buffer are the body, and the staging is closed. A body for a new profile
    // is written under the id the store assigns, which is reported back; a
    // body for the factory profile goes to the address that body sits at and
    // reports the fixed id it is read under. A body short of the length its
    // opening frame declared is not written at all; the staging is closed
    // either way, because the stream it belonged to is over once the frame
    // that ends it has been answered. Reports whether the store took the body,
    // and the id it is read back under.
    static bool commit(uint16_t *newId);

    // Drop whatever holds the buffer -- an open stream or a staged body -- and
    // answer it no further: what the frames that would have followed it must
    // not do is carry pieces of a body whose opening frame never went out, and
    // what the store must not be handed is a body that was refused on the way
    // in.
    static void abandon();
};

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
// what holds the buffer; the arms that open a staged write are met ahead of
// the gate, where a staging the host never ended is dropped and the opening
// frame takes the buffer itself rather than being refused for what it arrived
// over; every other arm, the two carried ones included, is refused while a
// body is on its way back to the host, because the buffer a stream is read out
// of is the buffer a staged body lands in. The arms behind the gate are
// declared below it and reached only through it, which is how a body is read
// from that buffer by no arm but a refused one.
//
// The two arms the domain serves that read no body -- the store's own status
// record and its list of the ids it carries -- are answered from the store's
// index and reach no staging buffer, so no gate stands in front of them and
// they are answered whatever a body's bytes are doing.
//
// The active profile is carried by the store alone: a body written becomes it,
// a select names one, and a delete that drops it leaves the store on the
// factory one. The configuration the device runs on reads the profile it
// belongs to -- the one a save writes to, and the one a read that names no
// profile reads -- from the store's reading of it, so no arm of this domain
// leaves the two apart.
class ProfileDomain {
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

    // Begin a profile body, naming the profile it is for: the factory profile,
    // which the body replaces, or a user profile, which is the same request
    // the arm above makes -- the store assigns the id of a new profile either
    // way, so the id in range names no profile, it asks for one. An opening
    // frame is answered with no frame of its own, as the arm above is.
    static Answers start(const ThetaGP_Request &request, ThetaGP_Reply &reply);

    // One piece of the body being staged, taken out of the frame that brought
    // it and appended to the staging buffer. A taken piece is answered with no
    // frame at all; a refused one is answered with the refusal it is owed, and
    // the staging is void with it.
    static Answers chunk(const uint8_t *payload, uint16_t length,
                         const ThetaGP_Request &request,
                         ThetaGP_Reply &reply);

    // The end of a staged body: the bytes the staging buffer holds are written
    // to the store as one body, and the answer is the opening arm's own
    // success arm, carrying the id the body is read back under and the length
    // written. A body short of the length its opening frame declared is
    // refused and never written.
    static void putEnd(ThetaGP_Reply &reply);

    // Drop a profile from the store: the store's id no longer answers, and the
    // body it held is not read back for anything but the write that replaced
    // it.
    static void remove(const ThetaGP_Request &request, ThetaGP_Reply &reply);

    // Make a profile the active one: the body the configuration the device
    // runs on is read from at the next boot.
    static void select(const ThetaGP_Request &request, ThetaGP_Reply &reply);

    // Write the configuration the device is running into the profile it
    // belongs to, which is the active one. The factory profile is not written
    // to: it is the body the device falls back to, and the request is refused
    // for the state it is in rather than answered with a write that failed.
    static void save(ThetaGP_Reply &reply);

    // Read a profile's body into the configuration the device is running,
    // starting from the compiled-in defaults. A request naming no profile
    // reads the active one, which is not refused: the field is optional so
    // that naming none and naming the factory profile are two requests. The
    // active profile is not changed by the read, whichever id the request
    // carries: the profile the configuration belongs to after the arm is the
    // one it belonged to before it.
    static void load(const ThetaGP_Request &request, ThetaGP_Reply &reply);
};

} // namespace ThetaGP::Wire
