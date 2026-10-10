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

#include "drivers/gp_emulator/ps4/ps4_driver.h"
#include "drivers/gp_emulator/ps4/ps4_descriptors.h"
#include "drivers/gp_emulator/shared/driver_helper.h"

#include "build_info.h"
#include "gamepad/gamepad.h"

#include "tusb.h"
#include <cstring>

namespace ThetaGP::Drivers::GPEmulator {

PS4Driver::PS4Driver() {}

static bool ps4_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                tusb_control_request_t const *request) {
  return hidd_control_xfer_cb(rhport, stage, request);
}

void PS4Driver::initialize() {
  report = {};
  report.report_id = PS4_REPORT_INPUT;
  report.left_stick_x = PS4_JOYSTICK_MID;
  report.left_stick_y = PS4_JOYSTICK_MID;
  report.right_stick_x = PS4_JOYSTICK_MID;
  report.right_stick_y = PS4_JOYSTICK_MID;
  report.buttons_1 = PS4_HAT_NOTHING;
  // Nothing is on the touchpad: both packets say the point is lifted, which is
  // what the device reports between touches.
  report.touch[0] = PS4_TOUCH_LIFTED;
  report.touch[4] = PS4_TOUCH_LIFTED;

  // The controller's MAC: the board hash's four bytes, then the chip serial's
  // low 16 bits. The first byte is made a locally administered unicast one.
  const uint32_t board = GPEmulator::get_string_hash_u32(BOARD_NAME);
  ps4_feature_mac[0] = static_cast<uint8_t>(board);
  ps4_feature_mac[1] = static_cast<uint8_t>(board >> 8);
  ps4_feature_mac[2] = static_cast<uint8_t>(board >> 16);
  ps4_feature_mac[3] = static_cast<uint8_t>(board >> 24);
  const uint16_t serial = static_cast<uint16_t>(DEVICE_UID_WORD(0));
  ps4_feature_mac[4] = static_cast<uint8_t>(serial);
  ps4_feature_mac[5] = static_cast<uint8_t>(serial >> 8);
  ps4_feature_mac[0] =
      static_cast<uint8_t>((ps4_feature_mac[0] & 0xFC) | 0x02);

  class_driver = {
#if CFG_TUSB_DEBUG >= 2
      .name = "PS4",
#endif
      .init = hidd_init,
      .deinit = nullptr,
      .reset = hidd_reset,
      .open = hidd_open,
      .control_xfer_cb = ps4_control_xfer_cb,
      .xfer_cb = hidd_xfer_cb,
      .xfer_isr = nullptr,
      .sof = NULL};
}

bool PS4Driver::process(void *gamepad) {
  ThetaGP::Gamepad::Gamepad *gp =
      reinterpret_cast<ThetaGP::Gamepad::Gamepad *>(gamepad);

  switch (gp->getState().dpad & GAMEPAD_MASK_DPAD) {
  case GAMEPAD_MASK_UP:
    report.buttons_1 = PS4_HAT_UP;
    break;
  case GAMEPAD_MASK_UP | GAMEPAD_MASK_RIGHT:
    report.buttons_1 = PS4_HAT_UPRIGHT;
    break;
  case GAMEPAD_MASK_RIGHT:
    report.buttons_1 = PS4_HAT_RIGHT;
    break;
  case GAMEPAD_MASK_DOWN | GAMEPAD_MASK_RIGHT:
    report.buttons_1 = PS4_HAT_DOWNRIGHT;
    break;
  case GAMEPAD_MASK_DOWN:
    report.buttons_1 = PS4_HAT_DOWN;
    break;
  case GAMEPAD_MASK_DOWN | GAMEPAD_MASK_LEFT:
    report.buttons_1 = PS4_HAT_DOWNLEFT;
    break;
  case GAMEPAD_MASK_LEFT:
    report.buttons_1 = PS4_HAT_LEFT;
    break;
  case GAMEPAD_MASK_UP | GAMEPAD_MASK_LEFT:
    report.buttons_1 = PS4_HAT_UPLEFT;
    break;
  default:
    report.buttons_1 = PS4_HAT_NOTHING;
    break;
  }

  report.left_stick_x = static_cast<uint8_t>(gp->getState().lx >> 8);
  report.left_stick_y = static_cast<uint8_t>(gp->getState().ly >> 8);
  report.right_stick_x = static_cast<uint8_t>(gp->getState().rx >> 8);
  report.right_stick_y = static_cast<uint8_t>(gp->getState().ry >> 8);

  // The four face buttons keep the order the HID mode keeps them in: this
  // project's B3, B1, B2 and B4 are the device's south, east, west and north,
  // which is where the numbers a host shows them under come from.
  report.buttons_1 |= (gp->pressedB2() ? PS4_MASK_SQUARE : 0) |
                      (gp->pressedB3() ? PS4_MASK_CROSS : 0) |
                      (gp->pressedB1() ? PS4_MASK_CIRCLE : 0) |
                      (gp->pressedB4() ? PS4_MASK_TRIANGLE : 0);

  report.buttons_2 = (gp->pressedL1() ? PS4_MASK_L1 : 0) |
                     (gp->pressedR1() ? PS4_MASK_R1 : 0) |
                     (gp->pressedL2() ? PS4_MASK_L2 : 0) |
                     (gp->pressedR2() ? PS4_MASK_R2 : 0) |
                     (gp->pressedS1() ? PS4_MASK_SHARE : 0) |
                     (gp->pressedS2() ? PS4_MASK_OPTIONS : 0) |
                     (gp->pressedL3() ? PS4_MASK_L3 : 0) |
                     (gp->pressedR3() ? PS4_MASK_R3 : 0);

  report.buttons_3 = (gp->pressedA1() ? PS4_MASK_PS : 0) |
                     (gp->pressedA2() ? PS4_MASK_TOUCHPAD : 0) | (counter & 0x3F);

  report.left_trigger = gp->pressedL2() ? 0xFF : 0;
  report.right_trigger = gp->pressedR2() ? 0xFF : 0;

  // The device stamps every report with a counter of its own clock: the host
  // reads it to tell one report from the next, and nothing depends on where
  // the count starts.
  timestamp = static_cast<uint16_t>(timestamp + 188);
  report.timestamp[0] = LSB16(timestamp);
  report.timestamp[1] = MSB16(timestamp);
  counter = static_cast<uint8_t>((counter + 1) & 0x3F);

  if (tud_suspended()) {
    tud_remote_wakeup();
  }

  return tud_hid_ready() && tud_hid_report(0, &report, sizeof(report));
}

uint16_t PS4Driver::get_report(uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
  if (report_type != HID_REPORT_TYPE_FEATURE) {
    // The report the device sends carries its id in the first byte and the
    // wire puts that byte in front of what this answers with, so the payload
    // starts after it and is no longer than the caller asked for.
    const uint8_t *payload_bytes = reinterpret_cast<const uint8_t *>(&report) + 1;
    uint16_t size = static_cast<uint16_t>(sizeof(report) - 1);
    if (size > reqlen) {
      size = reqlen;
    }
    std::memcpy(buffer, payload_bytes, size);
    return size;
  }

  const uint8_t *payload = nullptr;
  uint16_t size = 0;
  switch (report_id) {
  case PS4_REPORT_CALIBRATION:
    payload = ps4_feature_calibration;
    size = sizeof(ps4_feature_calibration);
    break;
  case PS4_REPORT_MAC:
    payload = ps4_feature_mac;
    size = sizeof(ps4_feature_mac);
    break;
  case PS4_REPORT_VERSION:
    payload = ps4_feature_version;
    size = sizeof(ps4_feature_version);
    break;
  default:
    // The authentication reports read as the state of a device that carries no
    // signature: a host that asks for one is answered with nothing to sign.
    return 0;
  }

  if (reqlen < size) {
    return 0;
  }
  std::memcpy(buffer, payload, size);
  return size;
}

void PS4Driver::set_report(uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
  (void)report_type;
  (void)buffer;
  (void)bufsize;
  // The device's own output reports carry a lightbar colour and a rumble level.
  // Nothing on this board is driven by them, so they are read here and dropped
  // where a driver for the board's own effect would pick them up.
  (void)report_id;
}

bool PS4Driver::vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                       tusb_control_request_t const *request) {
  (void)rhport;
  (void)stage;
  (void)request;
  return false;
}

const uint16_t *PS4Driver::get_descriptor_string_cb(uint8_t index,
                                                    uint16_t langid) {
  constexpr uint16_t kStringCount =
      sizeof(ps4_string_descriptors) / sizeof(ps4_string_descriptors[0]);
  // A host may name an index this set does not carry: past the last entry
  // there is no string to read, and the request is answered as unsupported
  // rather than read past the table's end.
  if (index >= kStringCount) {
    return nullptr;
  }
  char *value = (char *)ps4_string_descriptors[index];
  return getStringDescriptor(value, index);
}

const uint8_t *PS4Driver::get_descriptor_device_cb() {
  return ps4_device_descriptor;
}

const uint8_t *PS4Driver::get_hid_descriptor_report_cb(uint8_t itf) {
  (void)itf;
  return ps4_report_descriptor;
}

const uint8_t *PS4Driver::get_interface_descriptor() {
  return ps4_interface_descriptor;
}

uint16_t PS4Driver::get_interface_descriptor_size() {
  return sizeof(ps4_interface_descriptor);
}

const uint8_t *PS4Driver::get_descriptor_device_qualifier_cb() {
  return nullptr;
}

uint16_t PS4Driver::GetJoystickMidValue() {
  return PS4_JOYSTICK_MID << 8;
}

} // namespace ThetaGP::Drivers::GPEmulator
