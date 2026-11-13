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

#include <cstdio>
#include <cstring>

#include "conf/ThetaGP_Config.h"
#include "drivers/device/system_timer.h"
#include "drivers/gp_emulator/usb_driver.h"
#include "drivers/peripherals/peripherals_manager.h"
#include "gamepad/config/config_manager.h"
#include "sched/scheduler.h"
#include "utils/log/log.h"
#include "utils/utils.h"
#include "wire/frame.h"
#include "wire/profile_d.h"

#include "pb/Link.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"

using ThetaGP::Drivers::Peripheral::BUS::Mode;
using ThetaGP::Drivers::Peripheral::BUS::UartBus;

namespace ThetaGP::Drivers::Device {

#ifdef BDCFG_COPROCESSOR_UART

namespace {

// The schema's own version, as the greetings carry it.
constexpr uint32_t kProtocol = 1;
// How often this board says what it shows, and how long a silence is a link
// that is down rather than one that is quiet.
constexpr uint32_t kStateUs = 500000;
constexpr uint32_t kAliveUs = 1500000;
constexpr uint32_t kBoardUs = 2000000;
// One step is worth a moment on the panel: its mark travels a card in about
// half a second, and two steps inside that would only start over.
constexpr uint32_t kNavGapUs = 550000;
// Frames one call handles before leaving the rest to the next tick: the panel
// sends at most a couple a second, and a burst should not hold the run long.
constexpr uint8_t kDrainFrames = 4;

// Two buffers of the bus's own capacity, borrowed by it for the firmware's
// lifetime. AXI SRAM (COMMON_ZERO_INIT): the region the UART's DMA reaches.
COMMON_ZERO_INIT static uint8_t s_linkTx[UartBus::MAX_BUF_SIZE];
COMMON_ZERO_INIT static uint8_t s_linkRx[UartBus::MAX_BUF_SIZE];

void rxEntry(void *context) {
  if (context == nullptr) {
    return;
  }
  auto *link = static_cast<CoprocessorLink *>(context);
  link->received();
  // This runs in the UART's interrupt: the reading is posted, not done here.
  Sched::Scheduler::getInstance().postFromIsr(link->wakeTask());
}

// A fixed-width field, padded with zeroes so the message keeps one size.
void putField(char *field, size_t size, const char *text) {
  std::strncpy(field, text, size - 1);
  field[size - 1] = '\0';
}

// The footer's release and nothing after it: "0.3.0" rather than
// "0.3.0-64-ga65c632", which is all the panel has room to say.
void putRelease(char *field, size_t size) {
  char release[sizeof(THETAGP_FW_VERSION) + 1] = {};
  std::strncpy(release, THETAGP_FW_VERSION, sizeof release - 1);
  char *tail = std::strchr(release, '-');
  if (tail != nullptr) {
    *tail = '\0';
  }
  putField(field, size, release);
}

// One message, encoded into the payload buffer a frame carries.
bool pack(const ThetaGP_Link_Message &message, uint8_t *payload,
          uint16_t *length) {
  pb_ostream_t stream = pb_ostream_from_buffer(payload, *length);
  if (!pb_encode(&stream, ThetaGP_Link_Message_fields, &message)) {
    LOG_ERROR("link: encode refused: %s", PB_GET_ERROR(&stream));
    return false;
  }
  *length = static_cast<uint16_t>(stream.bytes_written);
  return true;
}

} // namespace

void CoprocessorLink::init() {
  _uart = &Peripheral::PeripheralsManager::getInstance().uartBus(
      BDCFG_COPROCESSOR_UART);
  _uart->setBuffers(s_linkTx, s_linkRx, sizeof s_linkTx);
  _uart->setMode(Mode::Dma);
  _uart->setRxCallback(rxEntry, this);
  _uart->init();

  reset();
  // The name the pages print is read once here, where nothing is waiting on a
  // deadline, and then only when the profile changes.
#if THETAGP_CFG_HAS_FLASH
  _profileNamedId = Gamepad::Config::ConfigManager::getInstance().activeProfileId();
  Wire::readActiveProfileName(_profileName, sizeof _profileName);
#endif
  _listening = isOk(_uart->receive(_rx, sizeof _rx));
  LOG_INFO("link: coprocessor uart %s", _listening ? "listening" : "not armed");
  setInitialized(_listening);
}

void CoprocessorLink::received() {
  if (_uart == nullptr) {
    return;
  }

  // The bus counts the bytes the idle line closed; the interrupt has already
  // copied them into _rx, and every one of them belongs to the frame being
  // assembled, whether this read carries all of it or a piece.
  const uint16_t landed = _uart->_readDmaBufLen;
  for (uint16_t i = 0; i < landed; ++i) {
    const uint16_t next =
        static_cast<uint16_t>((_head + 1) & (kStreamSize - 1));
    if (next == _tail) {
      _lost = _lost + 1;
      continue;
    }
    _stream[_head] = _rx[i];
    _head = next;
  }
  _landed = true;

  // The next frame's first byte follows the last one by a few microseconds at
  // this rate: the read has to be armed here, not when the task runs.
  (void)_uart->receive(_rx, sizeof _rx);
}

void CoprocessorLink::task() {
  if (_uart == nullptr || !_landed) {
    return;
  }

  _landed = false;
  drain();
}

void CoprocessorLink::drain() {
  uint8_t handled = 0;
  while (_tail != _head && handled < kDrainFrames) {
    const uint8_t byte = _stream[_tail];
    _tail = static_cast<uint16_t>((_tail + 1) & (kStreamSize - 1));
    if (parse(byte)) {
      handle(_payload, _filled);
      reset();
      ++handled;
    }
  }
}

void CoprocessorLink::tick() {
  if (_uart == nullptr) {
    return;
  }

  const uint32_t nowUs =
      Drivers::Device::SystemTimer::getInstance().getMicros();

  // A read that closed without a frame leaves the line armed for the next.
  if (!_landed && !_uart->isRxBusy()) {
    (void)_uart->receive(_rx, sizeof _rx);
  }

  _alive = _lastHeardUs != 0 && (nowUs - _lastHeardUs) < kAliveUs;

  // A board that has been heard is named again every couple of seconds: the
  // panel greets once every ten, and an answer lost to a busy transmitter
  // would otherwise leave it unnamed until then.
  if (_panelHeard && (nowUs - _lastBoardUs) >= kBoardUs) {
    _lastBoardUs = nowUs;
    sendBoard();
  }
  // Bytes the interrupt queued beyond one call's worth are taken here, a few
  // frames at a time, so nothing is stranded until the next burst.
  drain();

  flushNav();

  if (nowUs - _txLoggedUs >= 3000000U) {
    _txLoggedUs = nowUs;
    // A line only when a send or a frame was refused: the counters that never
    // move are not worth a line every three seconds.
    if (_txBusy != 0 || _txFail != 0 || _refused != 0) {
      LOG_INFO("link: tx ok=%u busy=%u fail=%u, rx=%u refused=%u",
               static_cast<unsigned>(_txOk), static_cast<unsigned>(_txBusy),
               static_cast<unsigned>(_txFail), static_cast<unsigned>(_frames),
               static_cast<unsigned>(_refused));
    }
  }

  if (nowUs - _lastStateUs >= kStateUs) {
    _lastStateUs = nowUs;
    sendState();
  }
}

void CoprocessorLink::reset() {
  _length = 0;
  _filled = 0;
  _sum = 0;
  _prefixBytes = 0;
  _checksumLow = 0;
  _checksumBytes = 0;
  _state = kPrefix;
}

bool CoprocessorLink::parse(uint8_t byte) {
  switch (_state) {
  case kPrefix:
    _length =
        _prefixBytes == 0
            ? static_cast<uint16_t>(byte & 0x7Fu)
            : static_cast<uint16_t>(_length | ((uint16_t)(byte & 0x7Fu) << 7));
    if (_prefixBytes == 0 && (byte & 0x80u) != 0) {
      // A length this small is one byte: anything else continues it.
      _prefixBytes = 1;
      return false;
    }
    if (_length == 0 || _length > PAYLOAD_MAX) {
      // Nothing carries an empty payload, and a length past the buffer is a
      // prefix to give up on.
      _refused = _refused + 1;
      reset();
      return false;
    }
    _filled = 0;
    _sum = 0;
    _checksumBytes = 0;
    _state = kPayload;
    return false;

  case kPayload:
    _payload[_filled++] = byte;
    _sum = static_cast<uint16_t>(_sum + byte);
    if (_filled == _length) {
      _state = kChecksum;
      _checksumBytes = 0;
    }
    return false;

  case kChecksum:
    if (_checksumBytes == 0) {
      _checksumLow = byte;
      _checksumBytes = 1;
      return false;
    }
    if (_sum != static_cast<uint16_t>(_checksumLow |
                                      (static_cast<uint16_t>(byte) << 8))) {
      // A frame that did not hold is dropped whole: the parser goes back to
      // the length that starts the next one, or every byte after a bad frame
      // would be read as that frame's tail.
      _refused = _refused + 1;
      reset();
      return false;
    }
    return true;

  default:
    reset();
    return false;
  }
}

void CoprocessorLink::handle(const uint8_t *payload, uint16_t length) {
  ThetaGP_Link_Message message = ThetaGP_Link_Message_init_zero;
  pb_istream_t stream = pb_istream_from_buffer(payload, length);
  if (!pb_decode(&stream, ThetaGP_Link_Message_fields, &message)) {
    // A frame that framed and checksummed but carries no message of this
    // schema: another build's, or noise the sum let through.
    _refused = _refused + 1;
    return;
  }

  _frames = _frames + 1;
  _lastHeardUs = Drivers::Device::SystemTimer::getInstance().getMicros();
  _alive = true;
  _panelHeard = true;

  switch (message.which_kind) {
  case ThetaGP_Link_Message_hello_tag:
    putField(_panelId, sizeof _panelId, message.kind.hello.panel_id);
    putField(_panelRelease, sizeof _panelRelease, message.kind.hello.release);
    LOG_INFO("link: panel %s %s %ux%u", _panelId, _panelRelease,
             static_cast<unsigned>(message.kind.hello.width),
             static_cast<unsigned>(message.kind.hello.height));
    sendBoard();
    break;

  case ThetaGP_Link_Message_beat_tag:
    if (message.kind.beat.page != _panelPage) {
      LOG_INFO("link: panel page %u item %u", (unsigned)message.kind.beat.page,
               (unsigned)message.kind.beat.item);
    }
    _panelPage = message.kind.beat.page;
    _panelItem = message.kind.beat.item;
    _panelUptimeS = message.kind.beat.uptime_s;
    _panelDropped = message.kind.beat.dropped;
    break;

  default:
    break;
  }
}

void CoprocessorLink::sendBoard() {
  ThetaGP_Link_Message message = ThetaGP_Link_Message_init_zero;
  message.which_kind = ThetaGP_Link_Message_board_tag;
  message.kind.board.protocol = kProtocol;
  putField(message.kind.board.board_id, sizeof message.kind.board.board_id,
           BOARD_IDENTIFIER);
  putRelease(message.kind.board.release, sizeof message.kind.board.release);

  uint8_t payload[PAYLOAD_MAX] = {};
  uint16_t length = sizeof payload;
  if (pack(message, payload, &length) && sendFrame(payload, length)) {
    _boards = _boards + 1;
  }
}

void CoprocessorLink::sendState() {
  uint32_t id = 0;
  uint32_t count = 0;
#if THETAGP_CFG_HAS_FLASH
  Gamepad::Config::ConfigManager &manager =
      Gamepad::Config::ConfigManager::getInstance();
  id = manager.activeProfileId();
  count = manager.profileCount();
#endif

  ThetaGP_Link_Message message = ThetaGP_Link_Message_init_zero;
  message.which_kind = ThetaGP_Link_Message_state_tag;
  message.kind.state.report_hz = THETAGP_CFG_USB_REPORT_RATE_HZ;
  message.kind.state.profile_id = id;
  message.kind.state.profile_count = count;
  putField(message.kind.state.profile_name,
           sizeof message.kind.state.profile_name, _profileName);
  message.kind.state.host_attached = get_usb_mounted();
  message.kind.state.storing = THETAGP_CFG_HAS_FLASH;

  uint8_t payload[PAYLOAD_MAX] = {};
  uint16_t length = sizeof payload;
  if (pack(message, payload, &length)) {
    (void)sendFrame(payload, length);
  }
}

void CoprocessorLink::profileChanged() {
#if THETAGP_CFG_HAS_FLASH
  // Called from the task that changed the profile: the body it just wrote or
  // read is the one the name comes from, and a beat follows at once.
  _profileNamedId =
      Gamepad::Config::ConfigManager::getInstance().activeProfileId();
  _profileName[0] = '\0';
  Wire::readActiveProfileName(_profileName, sizeof _profileName);
  _lastStateUs = 0;
#endif
}

bool CoprocessorLink::sendNav(uint8_t action) {
  const uint8_t next = static_cast<uint8_t>((_navHead + 1) % kNavQueue);
  if (next == _navTail) {
    return false;
  }

  _nav[_navHead] = NavStep{action};
  _navHead = next;

  // A transmitter that is free takes it now, so a step asked for from the
  // command task leaves without waiting for the tick.
  flushNav();
  return true;
}

void CoprocessorLink::flushNav() {
  while (_navTail != _navHead) {
    // A step that arrived while the last one is still travelling waits for it:
    // the panel moves one place at a time, and a burst of steps is a burst of
    // moves rather than one.
    const uint32_t nowUs =
        Drivers::Device::SystemTimer::getInstance().getMicros();
    if (_lastNavUs != 0 && (nowUs - _lastNavUs) < kNavGapUs) {
      return;
    }
    const NavStep &step = _nav[_navTail];

    ThetaGP_Link_Message message = ThetaGP_Link_Message_init_zero;
    message.which_kind = ThetaGP_Link_Message_nav_tag;
    message.kind.nav.action = static_cast<ThetaGP_Link_NavAction>(step.action);

    uint8_t payload[PAYLOAD_MAX] = {};
    uint16_t length = sizeof payload;
    if (!pack(message, payload, &length) || !sendFrame(payload, length)) {
      // The transmitter still had the last frame: the step waits for the next
      // tick, and gives up once it has waited longer than a step is worth.
      if (++_navTried >= kNavTries) {
        LOG_ERROR("link: nav %u dropped, the transmitter stayed busy",
                  static_cast<unsigned>(step.action));
        _navTried = 0;
        _navTail = static_cast<uint8_t>((_navTail + 1) % kNavQueue);
        continue;
      }
      return;
    }

    _navTried = 0;
    _lastNavUs = nowUs;
    _navTail = static_cast<uint8_t>((_navTail + 1) % kNavQueue);
  }
}

bool CoprocessorLink::sendFrame(const uint8_t *payload, uint16_t length) {
  const uint16_t size =
      Wire::Frame::encode(payload, length, _frame, sizeof _frame);
  if (size == 0) {
    return false;
  }
  const Result sent = _uart->transmit(_frame, size);
  if (isOk(sent)) {
    _txOk = _txOk + 1;
  } else if (sent == Result::Busy) {
    _txBusy = _txBusy + 1;
  } else {
    _txFail = _txFail + 1;
  }
  return isOk(sent);
}

#else

void CoprocessorLink::init() {}

void CoprocessorLink::task() {}

void CoprocessorLink::tick() {}

void CoprocessorLink::received() {}

void CoprocessorLink::profileChanged() {}

bool CoprocessorLink::sendNav(uint8_t) { return false; }

#endif

} // namespace ThetaGP::Drivers::Device
