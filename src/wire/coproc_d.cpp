/*
 * This file is a part of ThetaGP.
 */

#include "wire/coproc_d.h"

#include <cstring>

#include "BoardConfig.h"
#include "conf/ThetaGP_Config.h"
#include "drivers/device/coprocessor_link.h"
#include "wire/dispatch.h"

namespace ThetaGP::Wire {

#ifdef BDCFG_COPROCESSOR_UART

namespace {

using Drivers::Device::CoprocessorLink;

// The host's keys and the link's are the same six, in the same order: what a
// caller names is what the panel is told, with no layer in between deciding
// that one is another.
uint8_t linkAction(ThetaGP_CoprocAction action) {
    switch (action) {
    case ThetaGP_CoprocAction_COPROC_UP:
    case ThetaGP_CoprocAction_COPROC_DOWN:
    case ThetaGP_CoprocAction_COPROC_LEFT:
    case ThetaGP_CoprocAction_COPROC_RIGHT:
    case ThetaGP_CoprocAction_COPROC_CONFIRM:
    case ThetaGP_CoprocAction_COPROC_BACK:
        return static_cast<uint8_t>(action);
    default:
        return 0xFF;
    }
}

void copyField(char *out, size_t size, const char *text) {
    std::strncpy(out, text != nullptr ? text : "", size - 1);
    out[size - 1] = '\0';
}

void navAnswer(ThetaGP_Reply &reply) {
    CoprocessorLink &link = CoprocessorLink::getInstance();
    reply.which_kind = ThetaGP_Reply_coproc_nav_tag;
    reply.kind.coproc_nav.page = link.panelPage();
    reply.kind.coproc_nav.item = link.panelItem();
}

} // namespace

void CoprocDomain::state(ThetaGP_Reply &reply) {
    CoprocessorLink &link = CoprocessorLink::getInstance();
    reply.which_kind = ThetaGP_Reply_coproc_state_tag;
    ThetaGP_CoprocStateOk &ok = reply.kind.coproc_state;
    ok.attached = link.panelHeard() && link.alive();
    copyField(ok.panel_id, sizeof ok.panel_id, link.panelId());
    copyField(ok.release, sizeof ok.release, link.panelRelease());
    ok.page = link.panelPage();
    ok.item = link.panelItem();
    ok.uptime_s = link.panelUptimeS();
    ok.dropped = link.panelDropped();
}

void CoprocDomain::nav(const ThetaGP_Request &request, ThetaGP_Reply &reply) {
    const ThetaGP_CoprocNav &arm = request.kind.coproc_nav;
    const uint8_t action = linkAction(arm.action);
    if (action > static_cast<uint8_t>(ThetaGP_CoprocAction_COPROC_BACK)) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_VALUE_OUT_OF_RANGE, 0,
                     ThetaGP_CoprocAction_COPROC_BACK);
        return;
    }

    CoprocessorLink &link = CoprocessorLink::getInstance();
    if (!link.alive()) {
        // The panel has not been heard from: the step would go nowhere.
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_STATE,
                     ThetaGP_Reason_REASON_COPROCESSOR_SILENT);
        return;
    }
    if (!link.sendNav(action)) {
        // Four keys are already waiting: the host is asking faster than the
        // panel is moving, and the key it asked for did not enter the queue.
        writeFailure(reply, ThetaGP_ErrorCode_ERR_BUSY,
                     ThetaGP_Reason_REASON_NONE);
        return;
    }

    navAnswer(reply);
}

#else // BDCFG_COPROCESSOR_UART

void CoprocDomain::state(ThetaGP_Reply &reply) {
    reply.which_kind = ThetaGP_Reply_coproc_state_tag;
    reply.kind.coproc_state.attached = false;
}

void CoprocDomain::nav(const ThetaGP_Request &, ThetaGP_Reply &reply) {
    writeFailure(reply, ThetaGP_ErrorCode_ERR_NOT_SUPPORTED,
                 ThetaGP_Reason_REASON_NOT_IMPLEMENTED);
}

#endif

} // namespace ThetaGP::Wire
