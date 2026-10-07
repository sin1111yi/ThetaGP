/**
 * This file is a part of ThetaGP.
 *
 * ThetaGP is free software: you can redistribute it
 * and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * ThetaGP is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "build_info.h"
#include "utils/log/log.h"
#include "utils/utils.h"

#include "BoardConfig.h"
#include "conf/ThetaGP_Config.h" // THETAGP_CFG_USB_REPORT_RATE_HZ, the gamepad task rate this file registers

#include "gamepad/gamepad.h"
#include "drivers/device/system_timer.h"
#include "drivers/device/rgb_strip/rgb_strip.h"
#include "task_manager.h"

#include "tusb.h"

#include "ThetaGP.h"

#include "wire/frame.h"
#include "wire/dispatch.h"

using namespace ThetaGP;
using namespace ThetaGP::Gamepad;

namespace {

// The reply payload and the frame built around it, held across the command
// task's calls: one request is answered at a time. Both are allocated once and
// written off the hot path, so they sit in the system RAM, not in fast RAM.
COMMON_ZERO_INIT static uint8_t s_reply[Wire::Frame::PAYLOAD_MAX]{};
COMMON_ZERO_INIT static uint8_t s_frame[Wire::Frame::FRAME_MAX]{};

// How much of s_frame is still to go out. The CDC FIFO takes 64 bytes at a
// time, so a larger reply leaves over several calls instead of being cut off.
uint16_t s_txLength = 0;
uint16_t s_txSent = 0;

// The refusals the frame layer has counted, so the task answers a frame it had
// to refuse once and only once.
uint32_t s_refused = 0;

// Hand the CDC FIFO as much of the pending frame as it will take. Nothing
// waits here: a full FIFO ends the call, and the rest goes out on the next one.
static void pumpCdcTx(void) {
  while (s_txLength != 0) {
    const uint16_t left = static_cast<uint16_t>(s_txLength - s_txSent);
    const uint32_t room = tud_cdc_write_available();
    if (room == 0) {
      break;
    }
    const uint16_t chunk = (left < room) ? left : static_cast<uint16_t>(room);
    const uint32_t wrote = tud_cdc_write(s_frame + s_txSent, chunk);
    if (wrote == 0) {
      break;
    }
    s_txSent = static_cast<uint16_t>(s_txSent + wrote);
    tud_cdc_write_flush();
  }
  if (s_txSent >= s_txLength) {
    s_txLength = 0;
    s_txSent = 0;
  }
}

// Take a framed reply for sending, unless one is still on its way out.
static bool queueFrame(uint16_t length) {
  if (length == 0 || s_txLength != 0) {
    return false;
  }
  s_txLength = length;
  s_txSent = 0;
  return true;
}

} // namespace

FAST_CODE static void taskGamepadCore(uint32_t currentTimeUs) {
  UNUSED(currentTimeUs);

  // tud_task first: consume pending USB events and free the HID IN
  // endpoint before submitting the next report. Calling it after
  // process() drops ~2/3 of a 1kHz report stream (single-buffer HID:
  // a report submitted while the endpoint is still busy is discarded).
  tud_task();
  Gamepad::Gamepad::getInstance().process();
}

FAST_CODE static void taskCmdProc(uint32_t currentTimeUs) {
  UNUSED(currentTimeUs);

  // The 20 Hz command tick: move on what is still going out, drop a half frame
  // whose bytes stopped arriving, then answer the whole frames the USB
  // interrupt assembled. A reply is framed here and handed to the CDC FIFO;
  // tud_task() on the report tick is what moves those bytes out.
  Wire::Frame &codec = Wire::Frame::getInstance();
  codec.tick(Drivers::Device::SystemTimer::getInstance().getMillis());
  pumpCdcTx();

  // One reply at a time: while a frame is leaving, s_frame holds it and the
  // frames the codec assembled wait for a later tick.
  if (s_txLength != 0) {
    return;
  }

  // A frame the layer refused is answered with a transport error: the host
  // resends the command it could not get through.
  const uint32_t refused = codec.droppedFrames();
  if (refused != s_refused) {
    s_refused = refused;
    const uint16_t answered = Wire::Dispatch::frameRefused(
        codec.lastDrop(), s_reply, sizeof s_reply);
    if (answered != 0) {
      const uint16_t framed =
          Wire::Frame::encode(s_reply, answered, s_frame, sizeof s_frame);
      queueFrame(framed);
    }
    return;
  }

  Wire::Frame::Payload payload{};
  while (s_txLength == 0 && codec.take(payload)) {
    const uint16_t answered = Wire::Dispatch::answer(
        payload.bytes, payload.length, s_reply, sizeof s_reply);
    if (answered == 0) {
      continue;
    }
    const uint16_t framed =
        Wire::Frame::encode(s_reply, answered, s_frame, sizeof s_frame);
    if (framed == 0) {
      continue;
    }
    queueFrame(framed);
    pumpCdcTx();
  }

  // A stream's frames answer no frame of the host's, so they leave from here,
  // one per tick, and only while nothing else is in flight.
  if (s_txLength == 0) {
    const uint16_t carried =
        Wire::Dispatch::pending(s_reply, sizeof s_reply);
    if (carried != 0) {
      const uint16_t streamed =
          Wire::Frame::encode(s_reply, carried, s_frame, sizeof s_frame);
      if (streamed != 0) {
        queueFrame(streamed);
        pumpCdcTx();
      }
    }
  }
}

void ThetaGP::ThetaGamepad::registerTasks(void) {
  TaskManager::registerTask("GAMEPAD", "CORE", taskGamepadCore,
                            TASK_PERIOD_HZ(THETAGP_CFG_USB_REPORT_RATE_HZ),
                            TaskPriority::Realtime);
  TaskManager::registerTask("COMM", "CMD_PROC", taskCmdProc,
                            TASK_PERIOD_HZ(20), TaskPriority::Medium);
#ifdef BDCFG_LED_RGB_STRIP_PIN
  // The strip is cosmetic; sending a frame holds the CPU long enough to matter
  // against the report tick, so it runs below every task that carries input.
  TaskManager::registerTask("LED", "EFFECT", Drivers::Device::RgbStrip::task,
                            TASK_PERIOD_US(THETAGP_CFG_LED_TASK_PERIOD_US),
                            TaskPriority::Low);
#endif
}