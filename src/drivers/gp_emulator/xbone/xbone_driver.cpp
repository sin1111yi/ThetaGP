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

#include "drivers/gp_emulator/xbone/xbone_driver.h"
#include "drivers/device/system_timer.h"
#include "drivers/gp_emulator/shared/driver_helper.h"
#include "drivers/gp_emulator/xbone/xbone_descriptors.h"

#include "build_info.h"
#include "gamepad/gamepad.h"
#include "drivers/peripherals/systick.h"

#include "tusb.h"
#include <cstdio>
#include <cstring>

namespace ThetaGP::Drivers::GPEmulator {

// The packet handed to the endpoints leaves through the controller's DMA, so it
// lives in system RAM. The rest of the state is what the exchange between the
// device and its host has reached.
COMMON_ZERO_INIT static uint8_t s_report_buffer[XBONE_ENDPOINT_SIZE];
COMMON_ZERO_INIT static uint8_t s_out_buffer[XBONE_ENDPOINT_SIZE];
COMMON_ZERO_INIT static uint8_t s_endpoint_in;
COMMON_ZERO_INIT static uint8_t s_endpoint_out;

static XboxOneReport s_report;
static uint8_t s_last_core[XBONE_REPORT_CORE_SIZE];
static uint8_t s_sequence = 1;
static uint32_t s_opened_at;
static bool s_announced;
static bool s_powered;
static bool s_report_sent;
// When the last report left, and whether one has since the host opened the
// endpoint.
static uint32_t s_last_sent_ms;
static bool s_sent_since_open;
static bool s_guide_pressed;
static uint32_t s_keepalive_at;
static uint8_t s_keepalive_sequence = 1;

// The descriptor is larger than one packet, so it is sent in fragments that
// each carry the whole message's length on the first one and their own offset
// on the rest.
static uint16_t s_descriptor_sent;
static uint8_t s_descriptor_sequence;
static bool s_descriptor_pending;
static bool s_descriptor_waiting_ack;
static uint32_t s_descriptor_waiting_since;

static XboxOneCompatibleId s_compatible_id = {
    .total_length = sizeof(XboxOneCompatibleId),
    .version = 0x0100,
    .index = XBONE_DESC_COMPATIBLE_ID,
    .total_sections = 1,
    .reserved = {},
    .first_interface = 0,
    .reserved2 = 1,
    .compatible_id = {'X', 'G', 'I', 'P', '1', '0'},
    .sub_compatible_id = {},
    .reserved3 = {},
};

static constexpr uint32_t XBONE_ANNOUNCE_DELAY_MS = 500;
static constexpr uint32_t XBONE_KEEPALIVE_MS = 15000;
static constexpr uint32_t XBONE_ACK_TIMEOUT_MS = 2000;

// Sequence zero is not a message, so the count never lands on it.
static uint8_t next_sequence() {
  s_sequence = static_cast<uint8_t>(s_sequence + 1);
  if (s_sequence == 0) {
    s_sequence = 1;
  }
  return s_sequence;
}

static bool send_packet(uint8_t *packet, uint16_t length) {
  if (s_endpoint_in == 0 || !tud_ready() || length > sizeof(s_report_buffer)) {
    return false;
  }
  if (!usbd_edpt_claim(THETAGP_USB_RHPORT, s_endpoint_in)) {
    return false;
  }
  std::memcpy(s_report_buffer, packet, length);
  if (!usbd_edpt_xfer(THETAGP_USB_RHPORT, s_endpoint_in, s_report_buffer, length,
                      false)) {
    usbd_edpt_release(THETAGP_USB_RHPORT, s_endpoint_in);
    return false;
  }
  usbd_edpt_release(THETAGP_USB_RHPORT, s_endpoint_in);
  return true;
}

static bool send_message(uint8_t command, uint8_t options,
                         const uint8_t *payload, uint8_t length) {
  if (length > XBONE_ENDPOINT_SIZE - XBONE_HEADER_SIZE) {
    return false;
  }
  uint8_t packet[XBONE_ENDPOINT_SIZE] = {};
  packet[0] = command;
  packet[1] = options;
  packet[2] = next_sequence();
  packet[3] = length;
  if (length != 0) {
    std::memcpy(&packet[XBONE_HEADER_SIZE], payload, length);
  }
  return send_packet(packet, XBONE_HEADER_SIZE + length);
}

// An acknowledgement names the message it answers and how much of it arrived.
static bool send_ack(uint8_t sequence, uint8_t command, uint16_t received) {
  uint8_t packet[13] = {XBONE_CMD_ACK,
                        XBONE_OPTION_INTERNAL,
                        sequence,
                        9,
                        0x00,
                        command,
                        XBONE_CMD_INPUT,
                        static_cast<uint8_t>(received),
                        static_cast<uint8_t>(received >> 8),
                        0x00,
                        0x00,
                        0x00,
                        0x00};
  return send_packet(packet, sizeof(packet));
}

// The length field carries the payload length; a set high bit announces that
// the length continues in the bytes after it.
static bool read_length(const uint8_t *packet, uint16_t size, uint16_t *offset,
                        uint16_t *length) {
  uint16_t at = 3;
  uint32_t value = 0;
  for (uint8_t shift = 0; shift < 28 && at < size; shift += 7) {
    const uint8_t byte = packet[at++];
    value |= static_cast<uint32_t>(byte & 0x7F) << shift;
    if ((byte & 0x80) == 0) {
      *offset = at;
      *length = static_cast<uint16_t>(value);
      return true;
    }
  }
  return false;
}

static bool send_descriptor_fragment() {
  const uint16_t total =
      static_cast<uint16_t>(sizeof(xbone_device_descriptor_blob));
  const uint16_t remaining = static_cast<uint16_t>(total - s_descriptor_sent);
  const uint16_t chunk =
      remaining > XBONE_CHUNK_SIZE ? XBONE_CHUNK_SIZE : remaining;
  const bool first = (s_descriptor_sent == 0);
  const bool last = (remaining <= XBONE_CHUNK_SIZE);

  uint8_t packet[XBONE_ENDPOINT_SIZE] = {};
  uint8_t options = XBONE_OPTION_INTERNAL | XBONE_OPTION_CHUNKED;
  if (first) {
    options |= XBONE_OPTION_CHUNK_START;
  }
  if (first || last) {
    options |= XBONE_OPTION_NEEDS_ACK;
  }

  // The fragment offset, or the whole message's length on the first fragment.
  const uint16_t offset = first ? total : s_descriptor_sent;

  packet[0] = XBONE_CMD_DESCRIPTOR;
  packet[1] = options;
  packet[2] = s_descriptor_sequence;
  packet[3] = static_cast<uint8_t>(chunk);
  if (chunk != 0 && offset < 0x80) {
    packet[3] |= 0x80;
    packet[4] = 0x00;
    packet[5] = static_cast<uint8_t>(offset);
  } else {
    packet[4] = static_cast<uint8_t>((offset & 0x7F) | 0x80);
    packet[5] = static_cast<uint8_t>(offset >> 7);
  }
  if (chunk != 0) {
    std::memcpy(&packet[XBONE_HEADER_SIZE + 2],
                &xbone_device_descriptor_blob[s_descriptor_sent], chunk);
  }

  if (!send_packet(packet, XBONE_HEADER_SIZE + 2 + chunk)) {
    return false;
  }

  s_descriptor_sent = static_cast<uint16_t>(s_descriptor_sent + chunk);
  s_descriptor_waiting_ack = (options & XBONE_OPTION_NEEDS_ACK) != 0;
  s_descriptor_waiting_since = millis();
  if (chunk == 0) {
    // The fragment that carries nothing ends the message.
    s_descriptor_pending = false;
    s_descriptor_waiting_ack = false;
  }
  return true;
}

static void handle_out_packet(const uint8_t *packet, uint16_t size) {
  if (size < XBONE_HEADER_SIZE) {
    return;
  }

  const uint8_t command = packet[0];
  const uint8_t options = packet[1];
  const uint8_t sequence = packet[2];
  uint16_t offset = 0;
  uint16_t length = 0;
  if (!read_length(packet, size, &offset, &length)) {
    return;
  }
  const uint16_t payload = static_cast<uint16_t>(
      size > offset ? (size - offset) : 0);

  if ((options & XBONE_OPTION_NEEDS_ACK) != 0) {
    send_ack(sequence, command, payload);
  }

  switch (command) {
  case XBONE_CMD_ACK:
    s_descriptor_waiting_ack = false;
    break;
  case XBONE_CMD_DESCRIPTOR:
    s_descriptor_sent = 0;
    s_descriptor_sequence = sequence;
    s_descriptor_pending = true;
    s_descriptor_waiting_ack = false;
    break;
  case XBONE_CMD_POWER_MODE:
    s_powered = true;
    break;
  default:
    // The LED pattern, the rumble levels and the authentication exchange are
    // read and left alone: this board drives no light and no motor, and it
    // carries no security processor.
    break;
  }
}

static void xbone_init() {}

static void xbone_reset(uint8_t rhport) {
  (void)rhport;
  s_endpoint_in = 0;
  s_endpoint_out = 0;
  s_announced = false;
  s_powered = false;
  s_report_sent = false;
  s_sent_since_open = false;
  s_descriptor_pending = false;
  s_descriptor_waiting_ack = false;
  s_descriptor_sent = 0;
}

static uint16_t xbone_open(uint8_t rhport,
                           tusb_desc_interface_t const *itf_descriptor,
                           uint16_t max_length) {
  if (itf_descriptor->bInterfaceClass != TUSB_CLASS_VENDOR_SPECIFIC ||
      itf_descriptor->bInterfaceSubClass != XBONE_INTERFACE_CLASS ||
      itf_descriptor->bInterfaceProtocol != XBONE_INTERFACE_PROTOCOL) {
    return 0;
  }

  const uint16_t length =
      static_cast<uint16_t>(sizeof(tusb_desc_interface_t) +
                            itf_descriptor->bNumEndpoints *
                                sizeof(tusb_desc_endpoint_t));
  if (max_length < length || itf_descriptor->bNumEndpoints != 2) {
    return 0;
  }

  const uint8_t *endpoints = reinterpret_cast<const uint8_t *>(itf_descriptor) +
                             sizeof(tusb_desc_interface_t);
  uint8_t out = 0;
  uint8_t in = 0;
  if (!usbd_open_edpt_pair(rhport, endpoints, itf_descriptor->bNumEndpoints,
                           TUSB_XFER_INTERRUPT, &out, &in)) {
    return 0;
  }
  s_endpoint_out = out;
  s_endpoint_in = in;
  s_opened_at = millis();
  s_announced = false;
  s_powered = false;
  s_report_sent = false;
  s_sent_since_open = false;
  s_descriptor_pending = false;
  s_descriptor_waiting_ack = false;
  s_descriptor_sent = 0;
  usbd_edpt_xfer(rhport, s_endpoint_out, s_out_buffer, sizeof(s_out_buffer),
                 false);

  return length;
}

static bool xbone_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                  tusb_control_request_t const *request) {
  if (stage != CONTROL_STAGE_SETUP) {
    return true;
  }
  if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR &&
      request->bmRequestType_bit.direction == TUSB_DIR_IN &&
      request->bRequest == XBONE_REQUEST_OS_FEATURE &&
      request->wIndex == XBONE_DESC_COMPATIBLE_ID) {
    return tud_control_xfer(rhport, request, &s_compatible_id,
                            sizeof(s_compatible_id));
  }
  return false;
}

static bool xbone_xfer_cb(uint8_t rhport, uint8_t ep_addr,
                          xfer_result_t result, uint32_t xferred_bytes) {
  if (ep_addr == s_endpoint_out) {
    if (result == XFER_RESULT_SUCCESS && xferred_bytes <= sizeof(s_out_buffer)) {
      handle_out_packet(s_out_buffer, static_cast<uint16_t>(xferred_bytes));
    }
    usbd_edpt_xfer(rhport, s_endpoint_out, s_out_buffer, sizeof(s_out_buffer),
                   false);
  }
  return true;
}

XboxOneDriver::XboxOneDriver() {}

void XboxOneDriver::initialize() {
  s_report = {};
  s_report.command = XBONE_CMD_INPUT;
  s_report.length = XBONE_REPORT_CORE_SIZE;
  std::memset(s_last_core, 0, sizeof(s_last_core));

  // The id a host reads to tell one controller from another: the chip's unique
  // id as hex, and the same id's low bytes inside the announce.
  std::snprintf(reinterpret_cast<char *>(xbone_string_serial),
                sizeof(xbone_string_serial), "%08X%08X%08X",
                static_cast<unsigned int>(DEVICE_UID_WORD(0)),
                static_cast<unsigned int>(DEVICE_UID_WORD(1)),
                static_cast<unsigned int>(DEVICE_UID_WORD(2)));
  uint8_t *announce = xbone_announce;
  for (uint8_t i = 0; i < 8; i++) {
    announce[i] = static_cast<uint8_t>(
        DEVICE_UID_WORD(i / 4) >> (8 * (i % 4)));
  }

  class_driver = {
#if CFG_TUSB_DEBUG >= 2
      .name = "XboxOne",
#endif
      .init = xbone_init,
      .deinit = nullptr,
      .reset = xbone_reset,
      .open = xbone_open,
      .control_xfer_cb = xbone_control_xfer_cb,
      .xfer_cb = xbone_xfer_cb,
      .xfer_isr = nullptr,
      .sof = NULL};
}

bool XboxOneDriver::process(void *gamepad) {
  ThetaGP::Gamepad::Gamepad *gp =
      reinterpret_cast<ThetaGP::Gamepad::Gamepad *>(gamepad);
  const ThetaGP::Gamepad::GamepadRawInput &state = gp->getState();

  if (s_endpoint_in == 0 || !tud_ready()) {
    return false;
  }

  if (!s_announced) {
    if (millis() - s_opened_at < XBONE_ANNOUNCE_DELAY_MS) {
      return false;
    }
    if (!send_message(XBONE_CMD_ANNOUNCE,
                      XBONE_OPTION_INTERNAL | XBONE_OPTION_NEEDS_ACK,
                      xbone_announce, sizeof(xbone_announce))) {
      return false;
    }
    s_announced = true;
    return true;
  }

  if (s_descriptor_pending) {
    if (s_descriptor_waiting_ack &&
        millis() - s_descriptor_waiting_since < XBONE_ACK_TIMEOUT_MS) {
      return false;
    }
    s_descriptor_waiting_ack = false;
    return send_descriptor_fragment();
  }

  if (!s_powered) {
    return false;
  }

  if (millis() - s_keepalive_at > XBONE_KEEPALIVE_MS) {
    const uint8_t keepalive[4] = {0x80, 0x00, 0x00, 0x00};
    uint8_t packet[XBONE_ENDPOINT_SIZE] = {};
    packet[0] = XBONE_CMD_KEEPALIVE;
    packet[1] = XBONE_OPTION_INTERNAL;
    packet[2] = s_keepalive_sequence;
    packet[3] = sizeof(keepalive);
    std::memcpy(&packet[XBONE_HEADER_SIZE], keepalive, sizeof(keepalive));
    if (!send_packet(packet, XBONE_HEADER_SIZE + sizeof(keepalive))) {
      return false;
    }
    s_keepalive_at = millis();
    s_keepalive_sequence = s_keepalive_sequence == 0xFF || s_keepalive_sequence == 0
                               ? 1
                               : static_cast<uint8_t>(s_keepalive_sequence + 1);
    return true;
  }

  if (s_guide_pressed != gp->pressedA1()) {
    const uint8_t guide[2] = {static_cast<uint8_t>(gp->pressedA1() ? 0x01 : 0x00),
                              0x5B};
    if (!send_message(XBONE_CMD_VIRTUAL_KEY, XBONE_OPTION_INTERNAL, guide,
                      sizeof(guide))) {
      return false;
    }
    s_guide_pressed = gp->pressedA1();
    return true;
  }

  s_report.buttons_1 = (gp->pressedS2() ? XBONE_MASK_START : 0) |
                       (gp->pressedS1() ? XBONE_MASK_BACK : 0) |
                       (gp->pressedB1() ? XBONE_MASK_A : 0) |
                       (gp->pressedB2() ? XBONE_MASK_B : 0) |
                       (gp->pressedB3() ? XBONE_MASK_X : 0) |
                       (gp->pressedB4() ? XBONE_MASK_Y : 0);

  s_report.buttons_2 = (gp->pressedUp() ? XBONE_MASK_DPAD_UP : 0) |
                       (gp->pressedDown() ? XBONE_MASK_DPAD_DOWN : 0) |
                       (gp->pressedLeft() ? XBONE_MASK_DPAD_LEFT : 0) |
                       (gp->pressedRight() ? XBONE_MASK_DPAD_RIGHT : 0) |
                       (gp->pressedL1() ? XBONE_MASK_LB : 0) |
                       (gp->pressedR1() ? XBONE_MASK_RB : 0) |
                       (gp->pressedL3() ? XBONE_MASK_LS : 0) |
                       (gp->pressedR3() ? XBONE_MASK_RS : 0);

  // The triggers are ten bits wide; the state's eight bits are expanded by
  // repeating their high end.
  const uint16_t left_trigger =
      gp->pressedL2() ? 0x03FF
                      : static_cast<uint16_t>((state.lt << 2) | (state.lt >> 6));
  const uint16_t right_trigger =
      gp->pressedR2() ? 0x03FF
                      : static_cast<uint16_t>((state.rt << 2) | (state.rt >> 6));
  s_report.left_trigger = left_trigger;
  s_report.right_trigger = right_trigger;

  s_report.left_stick_x =
      static_cast<int16_t>(static_cast<int32_t>(state.lx) - 0x8000);
  s_report.left_stick_y =
      static_cast<int16_t>(0x7FFF - static_cast<int32_t>(state.ly));
  s_report.right_stick_x =
      static_cast<int16_t>(static_cast<int32_t>(state.rx) - 0x8000);
  s_report.right_stick_y =
      static_cast<int16_t>(0x7FFF - static_cast<int32_t>(state.ry));

  const uint8_t *core = reinterpret_cast<const uint8_t *>(&s_report) +
                        XBONE_HEADER_SIZE;
  // A report goes out when the pad changed, and one every kIdleReportMs
  // otherwise: the one a change produces can leave before the host's driver
  // begins to read, and a pad that then stands still would never be seen.
  const bool changed = std::memcmp(core, s_last_core, sizeof(s_last_core)) != 0;
  const uint32_t now = Drivers::Device::SystemTimer::getInstance().getMillis();
  if (s_sent_since_open && !changed && (now - s_last_sent_ms) < kIdleReportMs) {
    return s_report_sent;
  }

  s_report.sequence = next_sequence();
  if (!send_packet(reinterpret_cast<uint8_t *>(&s_report), sizeof(s_report))) {
    return false;
  }
  std::memcpy(s_last_core, core, sizeof(s_last_core));
  s_last_sent_ms = now;
  s_sent_since_open = true;
  s_report_sent = true;
  return true;
}

uint16_t XboxOneDriver::get_report(uint8_t report_id,
                                   hid_report_type_t report_type,
                                   uint8_t *buffer, uint16_t reqlen) {
  (void)report_id;
  (void)report_type;
  uint16_t size = static_cast<uint16_t>(sizeof(s_report));
  if (size > reqlen) {
    size = reqlen;
  }
  std::memcpy(buffer, &s_report, size);
  return size;
}

void XboxOneDriver::set_report(uint8_t report_id, hid_report_type_t report_type,
                               uint8_t const *buffer, uint16_t bufsize) {
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)bufsize;
}

bool XboxOneDriver::vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                           tusb_control_request_t const *request) {
  (void)rhport;
  (void)stage;
  (void)request;
  return false;
}

const uint16_t *XboxOneDriver::get_descriptor_string_cb(uint8_t index,
                                                        uint16_t langid) {
  (void)langid;
  if (index == 0xEE) {
    return xbone_string_os;
  }
  constexpr uint16_t kStringCount =
      sizeof(xbone_string_descriptors) / sizeof(xbone_string_descriptors[0]);
  if (index >= kStringCount) {
    return nullptr;
  }
  char *value = (char *)xbone_string_descriptors[index];
  return getStringDescriptor(value, index);
}

const uint8_t *XboxOneDriver::get_descriptor_device_cb() {
  return xbone_device_descriptor;
}

const uint8_t *XboxOneDriver::get_hid_descriptor_report_cb(uint8_t itf) {
  (void)itf;
  return nullptr;
}

const uint8_t *XboxOneDriver::get_interface_descriptor() {
  return xbone_interface_descriptor;
}

uint16_t XboxOneDriver::get_interface_descriptor_size() {
  return sizeof(xbone_interface_descriptor);
}

const uint8_t *XboxOneDriver::get_descriptor_device_qualifier_cb() {
  return nullptr;
}

uint16_t XboxOneDriver::GetJoystickMidValue() { return 0; }

} // namespace ThetaGP::Drivers::GPEmulator
