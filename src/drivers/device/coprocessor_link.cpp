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

#include "drivers/device/coprocessor_link.h"

#include "BoardConfig.h"

#include <cstring>
#include "drivers/peripherals/peripherals_manager.h"
#include "drivers/device/system_timer.h"
#include "utils/log/log.h"
#include "wire/frame.h"
#include "utils/utils.h"

using ThetaGP::Drivers::Peripheral::BUS::Mode;
using ThetaGP::Drivers::Peripheral::BUS::UartBus;

namespace ThetaGP::Drivers::Device {

#ifdef BDCFG_COPROCESSOR_UART

namespace {

// Two buffers of the bus's own capacity, borrowed by it for the firmware's
// lifetime. AXI SRAM (COMMON_DATA): the region the UART's DMA reaches.
COMMON_DATA static uint8_t s_linkTx[UartBus::MAX_BUF_SIZE] = {};
COMMON_DATA static uint8_t s_linkRx[UartBus::MAX_BUF_SIZE] = {};

void rxEntry(void *context) {
  if (context != nullptr) {
    static_cast<CoprocessorLink *>(context)->received();
  }
}

// A fixed-width field, padded with zeroes so the frame keeps one size.
void putField(uint8_t *field, size_t size, const char *text) {
  std::strncpy(reinterpret_cast<char *>(field), text, size);
}

// The frame as the protocol defines it: a varint length, the payload and the
// payload's byte sum in the low 16 bits, little endian. A frame whose length
// and checksum do not hold is refused whole.
bool frameHolds(const uint8_t *frame, uint16_t size) {
  if (size < 3) {
    return false;
  }

  uint16_t length = 0;
  uint16_t prefix = 1;
  if ((frame[0] & 0x80u) != 0) {
    if (size < 4) {
      return false;
    }
    length = static_cast<uint16_t>((frame[0] & 0x7Fu) |
                                   ((uint16_t)(frame[1] & 0x7Fu) << 7));
    prefix = 2;
  } else {
    length = frame[0];
  }
  if (static_cast<uint16_t>(prefix + length + 2) != size) {
    return false;
  }

  uint16_t sum = 0;
  for (uint16_t i = 0; i < length; ++i) {
    sum = static_cast<uint16_t>(sum + frame[prefix + i]);
  }
  const uint16_t carried =
      static_cast<uint16_t>(frame[prefix + length] |
                            ((uint16_t)frame[prefix + length + 1] << 8));
  return sum == carried;
}

} // namespace

void CoprocessorLink::init() {
  _uart = &Peripheral::PeripheralsManager::getInstance().uartBus(
      BDCFG_COPROCESSOR_UART);
  _uart->setBuffers(s_linkTx, s_linkRx, sizeof s_linkTx);
  _uart->setMode(Mode::Dma);
  _uart->setRxCallback(rxEntry, this);
  _uart->init();

  static const uint8_t ping[] = {'P', 'I', 'N', 'G'};
  (void)Wire::Frame::encode(ping, sizeof ping, _ping, sizeof _ping);

  uint8_t identity[REPLY_PAYLOAD_BYTES] = {};
  identity[0] = 'T';
  identity[1] = 'G';
  identity[2] = 'I';
  identity[3] = 'D';
  putField(identity + 4, 16, BOARD_IDENTIFIER);
  // The release, not the distance from it: "0.3.0" rather than "0.3.0-64-ga65c632",
  // which is all a 428x156 panel has room to say.
  char release[sizeof(THETAGP_FW_VERSION)] = {};
  std::strncpy(release, THETAGP_FW_VERSION, sizeof release - 1);
  char *tail = std::strchr(release, '-');
  if (tail != nullptr) {
    *tail = '\0';
  }
  putField(identity + 20, 20, release);
  _replyLength = Wire::Frame::encode(identity, sizeof identity, _reply,
                                     sizeof _reply);

  _listening = isOk(_uart->receive(_frame, FRAME_BYTES));
  LOG_INFO("link: coprocessor uart %s", _listening ? "listening" : "not armed");
  setInitialized(_listening);
}

void CoprocessorLink::task() {
  if (_uart == nullptr) {
    return;
  }

  const uint32_t nowUs =
      Drivers::Device::SystemTimer::getInstance().getMicros();
  if (_lastHeardUs == 0) {
    _lastHeardUs = nowUs;
  }

  if (!_landed) {
    // A read that closed without a frame leaves the line armed for the next.
    if (!_uart->isRxBusy()) {
      (void)_uart->receive(_frame, FRAME_BYTES);
    }
    // Two seconds of silence is a link to test from this side: a frame of our
    // own says whether the panel's read is the half that is missing.
    if (nowUs - _lastHeardUs > 2000000U) {
      // Every two seconds of silence, not once: a panel that was not listening
      // yet has to hear the next one.
      if (isOk(_uart->transmit(_ping, FRAME_BYTES))) {
        _speaking = true;
        _lastHeardUs = nowUs;
      }
    }
    return;
  }
  _landed = false;
  _lastHeardUs = nowUs;

  if (!frameHolds(_frame, FRAME_BYTES)) {
    _refused = _refused + 1;
    _alive = false;
  } else {
    _heartbeats = _heartbeats + 1;
    _alive = true;
    // The answer says who this is: the panel prints it while the link holds.
    if (isOk(_uart->transmit(_reply, _replyLength))) {
      _answered = _answered + 1;
    }
  }

  (void)_uart->receive(_frame, FRAME_BYTES);
}

#else

void CoprocessorLink::init() { return false; }

void CoprocessorLink::task() {}

#endif

} // namespace ThetaGP::Drivers::Device
