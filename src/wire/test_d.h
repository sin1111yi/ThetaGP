/*
 * This file is a part of ThetaGP.
 */

#pragma once

#include <cstdint>

#include "conf/ThetaGP_Config.h"
#include "protocol/ThetaGP.pb.h"

namespace ThetaGP::Wire {

// The test domain's arms. Each function fills one reply with the answer its own
// arm carries and sets the kind that names that answer. The arms the flash
// facility carries are served only by a build that has it; every arm a build
// does not serve is answered by the envelope as an unknown command.
class TestDomain {
public:
#if THETAGP_CFG_HAS_FLASH
    // The erase of the whole chip is declared and not served: the reply is the
    // refusal that says so.
    static void chipErase(ThetaGP_Reply &reply);

    // Erase the sector holding an address, then put the store back in step with
    // the chip the erase changed.
    static void eraseSector(const ThetaGP_Request &request,
                            ThetaGP_Reply &reply);

    // Run the profile store's compaction.
    static void compaction(ThetaGP_Reply &reply);

    // Set the bus mode the flash driver runs in.
    static void spiMode(const ThetaGP_Request &request, ThetaGP_Reply &reply);

    // What the flash chip is.
    static void flashInfo(ThetaGP_Reply &reply);

    // Open a stream over a run of flash bytes: the opening frame reports the
    // run, and the frames that carry it follow from the transfer unit.
    static void flashRead(const ThetaGP_Request &request, ThetaGP_Reply &reply);

    // Program a run of bytes to the chip. The bytes arrive in a callback field,
    // so they are collected out of the frame's own bytes.
    static void flashWrite(const uint8_t *payload, uint16_t length,
                           const ThetaGP_Request &request,
                           ThetaGP_Reply &reply);
#endif

    // The two commands every build carries.
    static void memInfo(ThetaGP_Reply &reply);
    static void keypadScan(ThetaGP_Reply &reply);
};

} // namespace ThetaGP::Wire
