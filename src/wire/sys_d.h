/*
 * This file is a part of ThetaGP.
 */

#pragma once

#include "pb/ThetaGP.pb.h"

namespace ThetaGP::Wire {

// The system domain's six arms. Each function fills one reply with the answer
// its own arm carries and sets the kind that names that answer.
class SysDomain {
public:
    static void ping(ThetaGP_Reply &reply);
    static void fwVersion(ThetaGP_Reply &reply);
    static void reset(ThetaGP_Reply &reply);

    // Whether a reset has been asked for and the reply to it has been built but
    // not yet answered on the wire. sys.reset asks for one; the command task
    // resets the device once that reply is out of the CDC's FIFO, because a
    // reset takes the wire down with it.
    static bool resetRequested();

    // The arm the firmware declares and does not serve: the reply is the
    // refusal that says so.
    static void enterDfu(ThetaGP_Reply &reply);

    // The record of the task the request names, or the refusal when no task
    // carries that id.
    static void taskInfo(const ThetaGP_Request &request, ThetaGP_Reply &reply);

    // The device's own readings: the task load, the two flash accounts and one
    // entry per memory region.
    static void usage(ThetaGP_Reply &reply);
};

} // namespace ThetaGP::Wire
