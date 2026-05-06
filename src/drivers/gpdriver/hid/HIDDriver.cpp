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

#include "drivers/gpdriver/hid/HIDDriver.h"
#include "drivers/gpdriver/hid/HIDDescriptors.h"
#include "drivers/gpdriver/shared/driverhelper.h"

#include "gamepad/gamepad.h"

#include "tusb.h"
#include <cstddef>

namespace ThetaGP::Drivers::GPDriver {

HIDDriver::HIDDriver() {}

static bool hid_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                tusb_control_request_t const *request) {
  return hidd_control_xfer_cb(rhport, stage, request);
}

void HIDDriver::initialize() {
  hidReport = {
      .buttons = 0,
      .direction = HID_HAT_NOTHING,
      .dummy = 0,
      .l_x_axis = HID_JOYSTICK_MID,
      .l_y_axis = HID_JOYSTICK_MID,
      .r_x_axis = HID_JOYSTICK_MID,
      .r_y_axis = HID_JOYSTICK_MID,
  };

  class_driver = {
#if CFG_TUSB_DEBUG >= 2
      .name = "HID",
#endif
      .init = hidd_init,
      .deinit = nullptr,
      .reset = hidd_reset,
      .open = hidd_open,
      .control_xfer_cb = hid_control_xfer_cb,
      .xfer_cb = hidd_xfer_cb,
      .xfer_isr = nullptr,
      .sof = NULL};
}

bool HIDDriver::process(void *gamepad) {
  ThetaGP::Gamepad::Gamepad *gp =
      reinterpret_cast<ThetaGP::Gamepad::Gamepad *>(gamepad);

  switch (gp->getState().dpad & GAMEPAD_MASK_DPAD) {
  case GAMEPAD_MASK_UP:
    hidReport.direction = HID_HAT_UP;
    break;
  case GAMEPAD_MASK_UP | GAMEPAD_MASK_RIGHT:
    hidReport.direction = HID_HAT_UPRIGHT;
    break;
  case GAMEPAD_MASK_RIGHT:
    hidReport.direction = HID_HAT_RIGHT;
    break;
  case GAMEPAD_MASK_DOWN | GAMEPAD_MASK_RIGHT:
    hidReport.direction = HID_HAT_DOWNRIGHT;
    break;
  case GAMEPAD_MASK_DOWN:
    hidReport.direction = HID_HAT_DOWN;
    break;
  case GAMEPAD_MASK_DOWN | GAMEPAD_MASK_LEFT:
    hidReport.direction = HID_HAT_DOWNLEFT;
    break;
  case GAMEPAD_MASK_LEFT:
    hidReport.direction = HID_HAT_LEFT;
    break;
  case GAMEPAD_MASK_UP | GAMEPAD_MASK_LEFT:
    hidReport.direction = HID_HAT_UPLEFT;
    break;
  default:
    hidReport.direction = HID_HAT_NOTHING;
    break;
  }

  hidReport.l_x_axis = static_cast<uint8_t>(gp->getState().lx >> 8);
  hidReport.l_y_axis = static_cast<uint8_t>(gp->getState().ly >> 8);
  hidReport.r_x_axis = static_cast<uint8_t>(gp->getState().rx >> 8);
  hidReport.r_y_axis = static_cast<uint8_t>(gp->getState().ry >> 8);

  // these first three buttons are in this unintuitive order to be compatible
  // with expectations, e.g. both PS3/4/5 modes and Switch modes map to HID as
  // B3 B4  ==  1 4
  // B1 B2  ==  2 3
  hidReport.buttons = 0 | (gp->pressedB1() ? GAMEPAD_MASK_B2 : 0) |
                      (gp->pressedB2() ? GAMEPAD_MASK_B3 : 0) |
                      (gp->pressedB3() ? GAMEPAD_MASK_B1 : 0) |
                      (gp->pressedB4() ? GAMEPAD_MASK_B4 : 0) |
                      (gp->pressedL1() ? GAMEPAD_MASK_L1 : 0) |
                      (gp->pressedR1() ? GAMEPAD_MASK_R1 : 0) |
                      (gp->pressedL2() ? GAMEPAD_MASK_L2 : 0) |
                      (gp->pressedR2() ? GAMEPAD_MASK_R2 : 0) |
                      (gp->pressedS1() ? GAMEPAD_MASK_S1 : 0) |
                      (gp->pressedS2() ? GAMEPAD_MASK_S2 : 0) |
                      (gp->pressedL3() ? GAMEPAD_MASK_L3 : 0) |
                      (gp->pressedR3() ? GAMEPAD_MASK_R3 : 0) |
                      (gp->pressedA1() ? GAMEPAD_MASK_A1 : 0) |
                      (gp->pressedA2() ? GAMEPAD_MASK_A2 : 0) |
                      (gp->pressedA3() ? GAMEPAD_MASK_A3 : 0) |
                      (gp->pressedA4() ? GAMEPAD_MASK_A4 : 0) |
                      (gp->pressedUp() ? GAMEPAD_MASK_DU : 0) |
                      (gp->pressedDown() ? GAMEPAD_MASK_DD : 0) |
                      (gp->pressedLeft() ? GAMEPAD_MASK_DL : 0) |
                      (gp->pressedRight() ? GAMEPAD_MASK_DR : 0) |
                      (gp->pressedE1() ? GAMEPAD_MASK_E1 : 0) |
                      (gp->pressedE2() ? GAMEPAD_MASK_E2 : 0) |
                      (gp->pressedE3() ? GAMEPAD_MASK_E3 : 0) |
                      (gp->pressedE4() ? GAMEPAD_MASK_E4 : 0) |
                      (gp->pressedE5() ? GAMEPAD_MASK_E5 : 0) |
                      (gp->pressedE6() ? GAMEPAD_MASK_E6 : 0) |
                      (gp->pressedE7() ? GAMEPAD_MASK_E7 : 0) |
                      (gp->pressedE8() ? GAMEPAD_MASK_E8 : 0);

  // Wake up TinyUSB device only when state changes while suspended
  if (tud_suspended()) {
    if (memcmp(last_report, &hidReport, sizeof(hidReport)) != 0) {
      tud_remote_wakeup();
    }
  }

  if (memcmp(last_report, &hidReport, sizeof(hidReport)) != 0) {
    if (tud_hid_ready() && tud_hid_report(0, &hidReport, sizeof(hidReport))) {
      memcpy(last_report, &hidReport, sizeof(hidReport));
      return true;
    }
  }

  return false;
}

uint16_t HIDDriver::get_report(uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
  UNUSED(report_id);
  UNUSED(report_type);
  UNUSED(reqlen);
  memcpy(buffer, &hidReport, sizeof(HIDReport));
  return sizeof(HIDReport);
}

void HIDDriver::set_report(uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)bufsize;
}

bool HIDDriver::vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                       tusb_control_request_t const *request) {
  (void)rhport;
  (void)stage;
  (void)request;
  return false;
}

const uint16_t *HIDDriver::get_descriptor_string_cb(uint8_t index,
                                                    uint16_t langid) {
  char *value = (char *)hid_string_descriptors[index];
  return getStringDescriptor(value, index);
}

const uint8_t *HIDDriver::get_descriptor_device_cb() {
  return (const uint8_t *)&hid_device_descriptor;
}

const uint8_t *HIDDriver::get_hid_descriptor_report_cb(uint8_t itf) {
  (void)itf;
  return hid_report_descriptor;
}

const uint8_t *HIDDriver::get_interface_descriptor() {
  return hid_interface_descriptor;
}

uint16_t HIDDriver::get_interface_descriptor_size() {
  return sizeof(hid_interface_descriptor);
}

const uint8_t *HIDDriver::get_descriptor_device_qualifier_cb() {
  return nullptr;
}

uint16_t HIDDriver::GetJoystickMidValue() { return HID_JOYSTICK_MID << 8; }

} // namespace ThetaGP::Drivers::GPDriver
