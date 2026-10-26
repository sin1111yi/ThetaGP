/*
 * This file is a part of ThetaGP.
 */

#pragma once

#include <cstdint>

#include "pb/ThetaGP.pb.h"

namespace ThetaGP::Wire {

// The profile domain's body stream: the one body on its way back to the host,
// and the one on its way to the store, held between the frames that carry it.
// One frame is built per call.
class ProfileTransfer {
public:
    // Whether a body's bytes are spoken for, in either direction.
    static bool busy();

    // ── the body on its way back to the host ──

    // Open a stream over `len` bytes of the body of `id`. Reports whether a
    // stream is open; the bytes stay where the caller put them until the
    // closing frame has been built.
    static bool open(uint16_t id, const char *data, uint16_t len);

    // Fill `reply` with the next frame of the open stream: a piece of the body
    // while bytes are left, the frame that closes the stream once they are
    // gone, nothing when no stream is open. Reports whether a frame was built.
    static bool next(ThetaGP_Reply &reply);

    // Whether the frame built last carries fewer body bytes than it was built
    // to carry.
    static bool shortFrame();

    // ── the body on its way to the store ──

    // What the frame that opened a staging said about the write it begins.
    struct Opening {
        // The arm the staging was opened by, as the request's own kind.
        pb_size_t arm = 0;
        // Whether that arm named the factory profile.
        bool factory = false;
        // The id the opening named: the profile a body replaces, or 0.
        uint16_t id = 0;
    };

    // Whether a body is being staged for the store.
    static bool writeOpen();

    // The length the opening frame declared for the staged body, and how many
    // of its bytes the staging buffer holds.
    static uint16_t writeTotal();
    static uint16_t writeReceived();

    // Open a staging for a body of `total` bytes, written to the profile the
    // opening frame names.
    static void beginWrite(uint16_t total, Opening opening);

    // The opening frame the staging in hand belongs to.
    static Opening opening();

    // What one piece of a staged body came to.
    enum class Piece {
        // The bytes are in the staging buffer and the stream stands past them.
        Taken,
        // No body is being staged.
        NoStaging,
        // The piece is empty, or longer than the body has left.
        WrongLength,
        // The piece does not start where the stream stands.
        WrongOffset,
    };

    // Append the piece this frame carries to the staged body. A refused piece
    // voids the staging, and nothing of it is written.
    static Piece put(const uint8_t *payload, uint16_t length,
                     const ThetaGP_Request &request);

    // Write the staged body to the store as one body and close the staging. A
    // body short of the length its opening frame declared is not written.
    // Reports whether the store took the body, and the id it is read back
    // under.
    static bool commit(uint16_t *newId);

    // Drop whatever holds the buffer, an open stream or a staged body, and
    // answer it no further.
    static void abandon();
};

// The profile domain: the arms it serves, and the dispatch from a request to
// the arm it names.
//
// The gate in front of the arms that read or write the store's staging buffer
// is answered here once, not by each arm. The two arms of a staged write pass
// it while a staged write holds the buffer, an arm that opens a staging is met
// ahead of it, and every other arm is refused while a body is on its way back
// to the host. The two arms that read no body -- the store's status and its
// list -- are answered whatever the buffer is doing.
//
// The active profile is the store's: a body written becomes it, a select names
// one, and a delete that drops it leaves the store on the factory one.
class ProfileDomain {
public:
    // What answering an arm produced.
    enum class Answers {
        // A frame of the host's stream was answered.
        Reply,
        // The answer to the end of a staged write: it belongs to the exchange,
        // and carries the zero the schema reserves for that.
        Exchange,
        // No frame: what the arm did is visible in the frame that ends the
        // write.
        Nothing,
        // No arm of this domain; the envelope refuses the request.
        NotMine,
    };

    // The arm the request names, answered into the reply. The payload is the
    // frame's own bytes, which the field a staged piece arrives in is read
    // from.
    static Answers handle(const uint8_t *payload, uint16_t length,
                          const ThetaGP_Request &request,
                          ThetaGP_Reply &reply);

    // The store's status record.
    static void status(ThetaGP_Reply &reply);

    // The profiles the store carries, the factory profile first and then
    // ascending id, each flagged with whether it is the active profile.
    static void list(ThetaGP_Reply &reply);

private:
    // The refusal an arm that reaches the staging buffer is owed while a body's
    // bytes are spoken for, and whether it answered it.
    static bool refusedWhileBodyBusy(ThetaGP_Reply &reply);

    // One profile's body, as the opening frame of a stream.
    static void get(const ThetaGP_Request &request, ThetaGP_Reply &reply);

    // Begin a profile body whose id the store assigns when it is written.
    // Answered with no frame: there is nothing to report until there is a body.
    static Answers create(const ThetaGP_Request &request,
                          ThetaGP_Reply &reply);

    // Begin a profile body, naming the profile it is for. Answered with no
    // frame, as the arm above is.
    static Answers start(const ThetaGP_Request &request, ThetaGP_Reply &reply);

    // One piece of the staged body, appended to the staging buffer. A taken
    // piece is answered with no frame; a refused one with its refusal.
    static Answers chunk(const uint8_t *payload, uint16_t length,
                         const ThetaGP_Request &request,
                         ThetaGP_Reply &reply);

    // The end of a staged body: the staging is written to the store as one
    // body, and the opening arm's success arm answers.
    static void putEnd(ThetaGP_Reply &reply);

    // Drop a profile from the store.
    static void remove(const ThetaGP_Request &request, ThetaGP_Reply &reply);

    // Make a profile the active one: the body the device reads at the next
    // boot.
    static void select(const ThetaGP_Request &request, ThetaGP_Reply &reply);

    // Write the running configuration into the active profile. The factory
    // profile is refused rather than answered with a write that failed.
    static void save(ThetaGP_Reply &reply);

    // Read a profile's body into the running configuration, starting from the
    // compiled-in defaults. A request naming no profile reads the active one,
    // and the active profile is left where it was.
    static void load(const ThetaGP_Request &request, ThetaGP_Reply &reply);
};

} // namespace ThetaGP::Wire
