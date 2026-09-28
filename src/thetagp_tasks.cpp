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
#include "drivers/led/led_effect.h"
#include "task_manager.h"

#include "tusb.h"

#include "ThetaGP.h"

#include "comm/frame_codec.h"
#include "comm/request_handler.h"

using namespace ThetaGP;
using namespace ThetaGP::Gamepad;

namespace {

// The reply payload and the frame built around it, held across the command
// task's calls: one request is answered at a time. Both are allocated once and
// written off the hot path, so they sit in the system RAM, not in fast RAM.
COMMON_ZERO_INIT static uint8_t s_reply[Comm::FrameCodec::PAYLOAD_MAX]{};
COMMON_ZERO_INIT static uint8_t s_frame[Comm::FrameCodec::FRAME_MAX]{};

// The refusals the frame layer has counted, so the task answers a frame it had
// to refuse once and only once.
uint32_t s_refused = 0;

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

  // The 20 Hz command tick: drop a half frame whose bytes stopped arriving,
  // then answer every whole frame the USB interrupt assembled. A reply is
  // framed and written to the CDC FIFO here; tud_task() on the report tick is
  // what moves those bytes out.
  Comm::FrameCodec &codec = Comm::FrameCodec::getInstance();
  codec.tick(Drivers::Device::SystemTimer::getInstance().getMillis());

  // A frame the layer refused is answered with a transport error: the host
  // resends the command it could not get through.
  const uint32_t refused = codec.droppedFrames();
  if (refused != s_refused) {
    s_refused = refused;
    const uint16_t answered = Comm::RequestHandler::frameRefused(
        codec.lastDrop(), s_reply, sizeof s_reply);
    if (answered != 0) {
      const uint16_t framed =
          Comm::FrameCodec::encode(s_reply, answered, s_frame, sizeof s_frame);
      if (framed != 0) {
        tud_cdc_write(s_frame, framed);
        tud_cdc_write_flush();
      }
    }
  }

  Comm::FrameCodec::Payload payload{};
  while (codec.take(payload)) {
    const uint16_t answered = Comm::RequestHandler::answer(
        payload.bytes, payload.length, s_reply, sizeof s_reply);
    if (answered == 0) {
      continue;
    }
    const uint16_t framed =
        Comm::FrameCodec::encode(s_reply, answered, s_frame, sizeof s_frame);
    if (framed == 0) {
      continue;
    }
    tud_cdc_write(s_frame, framed);
    tud_cdc_write_flush();
  }
}

void ThetaGP::ThetaGamepad::registerTasks(void) {
  TaskManager::registerTask("GAMEPAD", "CORE", taskGamepadCore,
                            TASK_PERIOD_HZ(THETAGP_CFG_USB_REPORT_RATE_HZ),
                            TaskPriority::Realtime);
  TaskManager::registerTask("COMM", "CMD_PROC", taskCmdProc,
                            TASK_PERIOD_HZ(20), TaskPriority::Medium);
  // The strip is cosmetic: its render is long enough to matter against the
  // report tick, so it runs below every task that carries input.
  TaskManager::registerTask("LED", "EFFECT", Drivers::Led::ledEffectTask,
                            TASK_PERIOD_US(THETAGP_CFG_LED_TASK_PERIOD_US),
                            TaskPriority::Low);
}