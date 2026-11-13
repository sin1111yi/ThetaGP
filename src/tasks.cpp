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
#include "conf/ThetaGP_Config.h"

#include "gamepad/gamepad.h"
#include "drivers/device/system_timer.h"
#include "drivers/device/rgb_strip/rgb_strip.h"
#include "drivers/device/run_led.h"
#include "sched/scheduler.h"

#include "tusb.h"

#include "ThetaGP.h"

#include "wire/frame.h"
#include "wire/dispatch.h"
#include "wire/flash.h"
#include "wire/profile_d.h"
#include "wire/sys_d.h"

#include "drivers/device/coprocessor_link.h"

using namespace ThetaGP;
using namespace ThetaGP::Gamepad;

namespace {

// The time each task asks to have clear before the next report release when it
// starts. It is not the length of a run: a wide reply keeps the command task
// running across report releases. A clearance that does not fit between two
// report releases, start guard included, is one the task never gets.
constexpr bool fitsReportPeriod(uint32_t clearanceUs) {
  return clearanceUs + Sched::kStartGuardUs <
         Sched::periodUs(THETAGP_CFG_USB_REPORT_RATE_HZ);
}

constexpr uint32_t kCommandClearanceUs = 50;
static_assert(fitsReportPeriod(kCommandClearanceUs),
              "command task clearance does not fit the report period");
constexpr uint32_t kSystemClearanceUs = 50;
static_assert(fitsReportPeriod(kSystemClearanceUs),
              "system task clearance does not fit the report period");
#ifdef BDCFG_COPROCESSOR_UART
constexpr uint32_t kLinkClearanceUs = 200;
static_assert(fitsReportPeriod(kLinkClearanceUs),
              "link task clearance does not fit the report period");
#endif
#if THETAGP_CFG_HAS_RGB_STRIP
constexpr uint32_t kLedClearanceUs = 250;
static_assert(fitsReportPeriod(kLedClearanceUs),
              "LED task clearance does not fit the report period");
#endif

// The reply payload and the frame built around it: one request is answered at
// a time. AXI SRAM (COMMON_ZERO_INIT), the region DMA reaches.
COMMON_ZERO_INIT static uint8_t s_reply[Wire::Frame::PAYLOAD_MAX]{};
COMMON_ZERO_INIT static uint8_t s_frame[Wire::Frame::FRAME_MAX]{};

// How much of s_frame is still to go out, and how much of it has: the CDC FIFO
// takes a chunk at a time, so a larger reply leaves over several calls.
uint16_t s_txLength = 0;
uint16_t s_txSent = 0;

// The refusal count the task has answered up to.
uint32_t s_refused = 0;

// Hands the CDC FIFO as much of the pending frame as it takes; a full FIFO ends
// the call and the rest goes out on a later one.
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

// Takes a framed reply for sending unless one is still on its way out.
static bool queueFrame(uint16_t length) {
  if (length == 0 || s_txLength != 0) {
    return false;
  }
  s_txLength = length;
  s_txSent = 0;
  return true;
}

} // namespace

FAST_CODE static void taskGamepadCore(void *context) {
  UNUSED(context);

  // tud_task first: it frees the HID IN endpoint, and a report submitted while
  // that endpoint is still busy is dropped.
  tud_task();
  Gamepad::Gamepad::getInstance().process();
}

FAST_CODE static void taskCmdProc(void *context) {
  UNUSED(context);

  // Moves the reply out, drops a half frame whose bytes stopped arriving, then
  // answers the frames the receive side assembled.
  Wire::Frame &codec = Wire::Frame::getInstance();
  codec.tick(Drivers::Device::SystemTimer::getInstance().getMillis());
  pumpCdcTx();

  // One reply at a time: the frames assembled wait for a later tick.
  if (s_txLength != 0) {
    return;
  }

  // A refused frame is answered once, with the transport error the host
  // resends on.
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
  const bool tookFrame = codec.take(payload);
  if (tookFrame) {
    const uint16_t answered = Wire::Dispatch::answer(
        payload.bytes, payload.length, s_reply, sizeof s_reply);
    if (answered != 0) {
      const uint16_t framed =
          Wire::Frame::encode(s_reply, answered, s_frame, sizeof s_frame);
      if (framed != 0) {
        queueFrame(framed);
        pumpCdcTx();
      }
    }
  }

  // A reset takes the wire down, so it waits until the reply has left the
  // FIFO.
  if (Wire::SysDomain::resetRequested() && s_txLength == 0 &&
      tud_cdc_n_write_available(0) == CFG_TUD_CDC_TX_BUFSIZE) {
    NVIC_SystemReset();
  }

  // A stream answers no frame of the host's: one frame per tick, while nothing
  // else is in flight. Both streams are asked whether they are open first,
  // because the call builds its reply before it finds out.
  if (!tookFrame && s_txLength == 0 &&
      (Wire::Flash::active() || Wire::ProfileTransfer::busy())) {
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

FAST_CODE static void taskSystemUpdate(void *context) {
  UNUSED(context);
  Drivers::Device::RunLed::getInstance().update(
      Drivers::Device::SystemTimer::getInstance().getMicros());
#ifdef BDCFG_COPROCESSOR_UART
  // The link's quiet half: keep its read armed, and speak after two seconds of
  // silence so a panel that is not talking can still be heard from here.
  Drivers::Device::CoprocessorLink::getInstance().tick();
#endif
}

#ifdef BDCFG_COPROCESSOR_UART
// Posted by the UART interrupt when a heartbeat lands, so the answer leaves in
// the same turn of the loop rather than on the next periodic tick.
static void taskCoprocessorLink(void *context) {
  UNUSED(context);
  Drivers::Device::CoprocessorLink::getInstance().task();
}
#endif

#if THETAGP_CFG_HAS_RGB_STRIP
static void taskLedEffect(void *context) {
  UNUSED(context);
  Drivers::Device::RgbStrip::task(
      Drivers::Device::SystemTimer::getInstance().getMicros());
}
#endif

void ThetaGP::ThetaGamepad::registerTasks(void) {
  using ThetaGP::Sched::Priority;
  using ThetaGP::Sched::Scheduler;
  using ThetaGP::Sched::periodUs;

  Scheduler &scheduler = Scheduler::getInstance();
  if (!scheduler.addPeriodic("GAMEPAD", "CORE", taskGamepadCore, nullptr,
                             periodUs(THETAGP_CFG_USB_REPORT_RATE_HZ),
                             Priority::Realtime)) {
    LOG_ERROR("scheduler: GAMEPAD.CORE was not registered");
  }
  // A command waits for this task's next tick, so the period is the latency a
  // caller pays per frame.
  if (!scheduler.addPeriodic("COMM", "CMD_PROC", taskCmdProc, nullptr,
                              periodUs(THETAGP_CFG_CMD_PROC_HZ),
                              Priority::Normal, 0, kCommandClearanceUs)) {
    LOG_ERROR("scheduler: COMM.CMD_PROC was not registered");
  }
#if THETAGP_CFG_HAS_RGB_STRIP
  // The strip is cosmetic: it runs below every task that carries input.
  if (!scheduler.addPeriodic("LED", "EFFECT", taskLedEffect, nullptr,
                              THETAGP_CFG_LED_TASK_PERIOD_US, Priority::Low,
                              0, kLedClearanceUs)) {
    LOG_ERROR("scheduler: LED.EFFECT was not registered");
  }
#endif
  if (!scheduler.addPeriodic("SYSTEM", "UPDATE", taskSystemUpdate, nullptr,
                              periodUs(1000), Priority::Low, 0, kSystemClearanceUs)) {
    LOG_ERROR("scheduler: SYSTEM.UPDATE was not registered");
  }
#ifdef BDCFG_COPROCESSOR_UART
  // The panel posts a frame at a couple of hertz, and the interrupt posts this
  // task for each one: at 50 Hz the longest a frame waits behind a task that is
  // already running is 20 ms, and a run of it passes 2 us.
  if (!scheduler.addPeriodic("LINK", "PANEL", taskCoprocessorLink, nullptr,
                              periodUs(50), Priority::Low, 0, kLinkClearanceUs)) {
    LOG_ERROR("scheduler: LINK.PANEL was not registered");
  }
#endif
}
