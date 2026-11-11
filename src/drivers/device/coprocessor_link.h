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

// The panel board as a device of this one: the coprocessor sends a heartbeat,
// this board answers with the frame it received, and the exchange is what says
// both ends are alive. Payloads stay opaque until the protocol is wired in.

#pragma once

#include "drivers/device/device.h"
#include "drivers/peripherals/bus/uart_bus.h"

#include "sched/core.h"

#include <cstdint>

namespace ThetaGP::Drivers::Device {

class CoprocessorLink : public Device {
public:
  static CoprocessorLink &getInstance() {
    static CoprocessorLink instance;
    return instance;
  }

  // Takes the UART the board binds to the coprocessor and arms the first read.
  void init() override;
  [[nodiscard]] bool listening() const { return _listening; }

  // The task the interrupt wakes: answers the heartbeat that landed and arms
  // the read for the next one.
  void task();

  // The periodic half: keeps the read armed and, after two seconds of silence,
  // says something so a quiet panel can still be heard from this side.
  void tick();

  // The task the UART interrupt posts, once the scheduler has handed one out.
  void setWakeTask(Sched::TaskIndex task) { _wakeTask = task; }
  [[nodiscard]] Sched::TaskIndex wakeTask() const { return _wakeTask; }

  // Called from the UART interrupt once the idle line closes a frame.
  void received() { _landed = true; }

  [[nodiscard]] bool speaking() const { return _speaking; }

  [[nodiscard]] bool alive() const { return _alive; }
  [[nodiscard]] uint32_t heartbeats() const { return _heartbeats; }
  [[nodiscard]] uint32_t answered() const { return _answered; }
  [[nodiscard]] uint32_t refused() const { return _refused; }

  // One heartbeat on the wire: a one-byte length, four payload bytes and the
  // checksum. The coprocessor sends the same shape, so the read is armed for
  // exactly this many bytes.
  static constexpr uint16_t FRAME_BYTES = 7;

  // What a heartbeat is answered with: the board's identifier and the firmware
  // version, so the panel can say whose display it is. Four magic bytes, then
  // sixteen and twenty fixed-width fields, framed.
  static constexpr uint16_t REPLY_PAYLOAD_BYTES = 40;
  static constexpr uint16_t REPLY_BYTES = 3 + REPLY_PAYLOAD_BYTES;

private:
  CoprocessorLink() : Device("coprocessor") {}

  Peripheral::BUS::UartBus *_uart = nullptr;
  bool _listening = false;

  // The frame as it arrived. DMA writes into the bus's own buffer, so this is
  // only touched by the task.
  uint8_t _frame[FRAME_BYTES] = {};

  Sched::TaskIndex _wakeTask = Sched::kInvalidTask;

  volatile bool _landed = false;
  bool _alive = false;
  bool _speaking = false;

  // Sent when nothing has arrived for a while, so a silent panel is still
  // something this board can be heard by.
  uint8_t _ping[FRAME_BYTES] = {};
  uint8_t _reply[REPLY_BYTES] = {};
  uint16_t _replyLength = 0;
  uint32_t _lastHeardUs = 0;
  uint32_t _heartbeats = 0;
  uint32_t _answered = 0;
  uint32_t _refused = 0;
};

} // namespace ThetaGP::Drivers::Device
