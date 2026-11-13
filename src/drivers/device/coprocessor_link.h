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

// The panel board as a device of this one: the messages of Link.proto over a
// UART. The panel greets, this board answers with what it is and then says
// what it shows every half second; the panel says what is on its screen, and a
// navigation step from here moves it.

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

  // Claims the UART the board binds to the coprocessor and arms the first read.
  void init() override;
  [[nodiscard]] bool listening() const { return _listening; }

  // The task the interrupt wakes: takes what the interrupt queued and handles
  // it, at most a few frames per call so one burst cannot hold the scheduler's
  // tasks off for a whole tick. The rest waits for `tick`.
  void task();

  // The periodic half: keeps the read armed and says what this board shows, so
  // a panel that missed one costs half a second at most.
  void tick();

  // Called from the UART interrupt once the idle line closed a read: the bus
  // knows how many bytes landed, and the interrupt has copied them already.
  // The read is armed again here, before the interrupt returns: the next frame
  // starts within a few microseconds, and a read armed by the task would come
  // back to a line that has already gone quiet.
  void received();

  void setWakeTask(Sched::TaskIndex task) { _wakeTask = task; }
  [[nodiscard]] Sched::TaskIndex wakeTask() const { return _wakeTask; }

  // The profile the pages name has changed: the name is read from the store
  // here, on the task that changed it, and the next beat carries it. Nothing
  // on the periodic tick reads the flash.
  void profileChanged();

  // One key for the panel. The key is queued and leaves on the periodic tick,
  // which retries a transmitter that was carrying a frame; false means the
  // queue is full and the key did not enter it.
  bool sendNav(uint8_t action);

  [[nodiscard]] bool alive() const { return _alive; }
  [[nodiscard]] uint32_t receivedFrames() const { return _frames; }
  [[nodiscard]] uint32_t boards() const { return _boards; }
  [[nodiscard]] uint32_t refused() const { return _refused; }

  // What the panel last said it is and shows.
  [[nodiscard]] const char *panelId() const { return _panelId; }
  [[nodiscard]] const char *panelRelease() const { return _panelRelease; }
  [[nodiscard]] bool panelHeard() const { return _panelHeard; }
  [[nodiscard]] uint32_t panelPage() const { return _panelPage; }
  [[nodiscard]] uint32_t panelItem() const { return _panelItem; }
  [[nodiscard]] uint32_t panelUptimeS() const { return _panelUptimeS; }
  [[nodiscard]] uint32_t panelDropped() const { return _panelDropped; }

  // Largest payload a frame of this link carries: one Link.proto message.
  static constexpr uint16_t PAYLOAD_MAX = 128;
  static constexpr uint16_t FRAME_MAX = 2 + PAYLOAD_MAX + 2;

private:
  CoprocessorLink() : Device("coprocessor") {}

  // The steps one frame is read in, as the wire defines them.
  enum : uint8_t { kPrefix = 0, kPayload = 1, kChecksum = 2 };

  struct NavStep {
    uint8_t action;
  };

  // The steps waiting for the transmitter, and how many ticks the one at the
  // head has been retried: a step that cannot leave in that time is dropped so
  // a stuck wire does not hold the queue for ever.
  static constexpr uint8_t kNavQueue = 4;
  static constexpr uint16_t kNavTries = 100;
  NavStep _nav[kNavQueue] = {};
  uint8_t _navHead = 0;
  uint8_t _navTail = 0;
  uint16_t _navTried = 0;
  uint32_t _lastNavUs = 0;

  // Sends the step at the head of the queue, if the transmitter will take it.
  void flushNav();

  // Handles what the interrupt queued, at most kDrainFrames whole messages.
  void drain();

  bool parse(uint8_t byte);
  void reset();
  void handle(const uint8_t *payload, uint16_t length);
  void sendBoard();
  void sendState();
  bool sendFrame(const uint8_t *payload, uint16_t length);

  Peripheral::BUS::UartBus *_uart = nullptr;
  bool _listening = false;

  // What one read landed in. DMA writes into the bus's own buffer, and the
  // interrupt hands it here: only the task reads this.
  uint8_t _rx[Peripheral::BUS::UartBus::MAX_BUF_SIZE] = {};
  volatile bool _landed = false;

  // What the interrupt has taken off the line and the task has not read yet.
  // The panel writes a frame in bursts, so a read can close inside one: the
  // bytes are queued whole and parsed across them.
  static constexpr uint16_t kStreamSize = 512;
  uint8_t _stream[kStreamSize] = {};
  volatile uint16_t _head = 0;
  volatile uint16_t _tail = 0;
  volatile uint32_t _lost = 0;

  // The frame being assembled from what landed.
  uint8_t _payload[PAYLOAD_MAX] = {};
  uint16_t _length = 0;
  uint16_t _filled = 0;
  uint16_t _sum = 0;
  uint8_t _prefixBytes = 0;
  uint8_t _checksumLow = 0;
  uint8_t _checksumBytes = 0;
  uint8_t _state = kPrefix;

  Sched::TaskIndex _wakeTask = Sched::kInvalidTask;

  volatile bool _alive = false;
  volatile uint32_t _frames = 0;
  volatile uint32_t _boards = 0;
  volatile uint32_t _refused = 0;
  uint32_t _txOk = 0;
  uint32_t _txBusy = 0;
  uint32_t _txFail = 0;
  uint32_t _txLoggedUs = 0;

  // The panel's own last word.
  char _panelId[21] = {};
  char _panelRelease[21] = {};
  uint32_t _panelPage = 0;
  uint32_t _panelItem = 0;
  uint32_t _panelUptimeS = 0;
  // The name the active profile's body carries. It is read from the store when
  // the profile changes and not on every beat: the body is in the flash, and a
  // beat has no time for a read.
  char _profileName[25] = {};
  uint32_t _profileNamedId = 0xFFFFFFFFu;
  uint32_t _panelDropped = 0;
  volatile bool _panelHeard = false;
  uint32_t _lastBoardUs = 0;
  uint32_t _lastHeardUs = 0;
  uint32_t _lastStateUs = 0;

  uint8_t _frame[FRAME_MAX] = {};
};

} // namespace ThetaGP::Drivers::Device
