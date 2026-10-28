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

#include "drivers/gp_emulator/xinput/xinput_driver.h"
#include "drivers/device/system_timer.h"
#include "drivers/gp_emulator/shared/driver_helper.h"
#include "drivers/gp_emulator/xinput/xinput_descriptors.h"

#include "build_info.h"
#include "gamepad/gamepad.h"

#include "tusb.h"
#include <cstdio>
#include <cstring>

namespace ThetaGP::Drivers::GPEmulator {

// The report handed to the input endpoint and the packet the output endpoint is
// armed with both leave through the controller's DMA, so they live in system
// RAM. The endpoint addresses are what the open callback found in the
// configuration descriptor.
COMMON_ZERO_INIT static uint8_t s_report_buffer[XINPUT_REPORT_SIZE];
COMMON_ZERO_INIT static uint8_t s_out_buffer[XINPUT_OUT_SIZE];
COMMON_ZERO_INIT static uint8_t s_endpoint_in;
COMMON_ZERO_INIT static uint8_t s_endpoint_out;

static void xinput_init() {}

static void xinput_reset(uint8_t rhport) {
  (void)rhport;
  s_endpoint_in = 0;
  s_endpoint_out = 0;
}

// A host asks the interface what the device can do before it reads reports from
// it. Both answers are fixed by the controller the device reports as, and the
// device-wide request carries the serial that tells two controllers apart.
static bool xinput_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                   tusb_control_request_t const *request) {
  if (stage != CONTROL_STAGE_SETUP) {
    // The data and status stages of a request answered here carry nothing the
    // device has to fill in.
    return true;
  }

  if (request->bmRequestType_bit.type != TUSB_REQ_TYPE_VENDOR ||
      request->bmRequestType_bit.direction != TUSB_DIR_IN) {
    return false;
  }

  uint8_t *payload = nullptr;
  uint16_t size = 0;
  if (request->bRequest == XINPUT_REQUEST_CAPABILITIES) {
    switch (request->bmRequestType_bit.recipient) {
    case TUSB_REQ_RCPT_INTERFACE:
      if (request->wValue == XINPUT_CAPABILITIES_VIBRATION) {
        payload = xinput_vibration_capabilities;
        size = sizeof(xinput_vibration_capabilities);
      } else if (request->wValue == XINPUT_CAPABILITIES_INPUT) {
        payload = xinput_input_capabilities;
        size = sizeof(xinput_input_capabilities);
      }
      break;
    case TUSB_REQ_RCPT_DEVICE:
      payload = xinput_serial_capability;
      size = sizeof(xinput_serial_capability);
      break;
    default:
      break;
    }
  }

  if (payload == nullptr) {
    return false;
  }
  if (size > request->wLength) {
    size = request->wLength;
  }
  return tud_control_xfer(rhport, request, payload, size);
}

// The output endpoint receives the LED pattern and the two motor levels. The
// board has neither, so the packet is read and the endpoint armed again.
static bool xinput_xfer_cb(uint8_t rhport, uint8_t ep_addr,
                           xfer_result_t result, uint32_t xferred_bytes) {
  (void)xferred_bytes;
  if (ep_addr == s_endpoint_out && result == XFER_RESULT_SUCCESS) {
    usbd_edpt_xfer(rhport, s_endpoint_out, s_out_buffer, sizeof(s_out_buffer),
                   false);
  }
  return true;
}

static uint16_t xinput_open(uint8_t rhport,
                            tusb_desc_interface_t const *itf_descriptor,
                            uint16_t max_length) {
  if (itf_descriptor->bInterfaceClass != TUSB_CLASS_VENDOR_SPECIFIC ||
      itf_descriptor->bInterfaceSubClass != XINPUT_INTERFACE_SUBCLASS ||
      itf_descriptor->bInterfaceProtocol != XINPUT_INTERFACE_PROTOCOL) {
    return 0;
  }

  const uint16_t length =
      static_cast<uint16_t>(sizeof(tusb_desc_interface_t) +
                            XINPUT_CAPABILITY_DESC_SIZE +
                            itf_descriptor->bNumEndpoints *
                                sizeof(tusb_desc_endpoint_t));
  if (max_length < length || itf_descriptor->bNumEndpoints != 2) {
    return 0;
  }

  const uint8_t *capability = reinterpret_cast<const uint8_t *>(itf_descriptor) +
                              sizeof(tusb_desc_interface_t);
  if (capability[1] != XINPUT_CAPABILITY_DESC_TYPE) {
    return 0;
  }

  const uint8_t *endpoints = capability + XINPUT_CAPABILITY_DESC_SIZE;
  uint8_t out = 0;
  uint8_t in = 0;
  if (!usbd_open_edpt_pair(rhport, endpoints, itf_descriptor->bNumEndpoints,
                           TUSB_XFER_INTERRUPT, &out, &in)) {
    return 0;
  }
  s_endpoint_out = out;
  s_endpoint_in = in;
  usbd_edpt_xfer(rhport, s_endpoint_out, s_out_buffer, sizeof(s_out_buffer),
                 false);

  return length;
}

XInputDriver::XInputDriver() {}

void XInputDriver::initialize() {
  report = {};
  report.report_size = XINPUT_REPORT_SIZE;
  // The sticks report zero at rest: this protocol reads them as signed values
  // around the middle.
  report.left_stick_x = 0;
  report.left_stick_y = 0;
  report.right_stick_x = 0;
  report.right_stick_y = 0;
  std::memset(last_report, 0, sizeof(last_report));

  // The serial is the chip's unique id, so two boards do not report the same
  // controller.
  std::snprintf(reinterpret_cast<char *>(xinput_string_serial),
                sizeof(xinput_string_serial), "%08X%08X%08X",
                static_cast<unsigned int>(DEVICE_UID_WORD(0)),
                static_cast<unsigned int>(DEVICE_UID_WORD(1)),
                static_cast<unsigned int>(DEVICE_UID_WORD(2)));
  const uint32_t serial =
      static_cast<uint32_t>(DEVICE_UID_WORD(0)) ^
      static_cast<uint32_t>(DEVICE_UID_WORD(1));
  xinput_serial_capability[0] = static_cast<uint8_t>(serial >> 24);
  xinput_serial_capability[1] = static_cast<uint8_t>(serial >> 16);
  xinput_serial_capability[2] = static_cast<uint8_t>(serial >> 8);
  xinput_serial_capability[3] = static_cast<uint8_t>(serial);

  class_driver = {
#if CFG_TUSB_DEBUG >= 2
      .name = "XInput",
#endif
      .init = xinput_init,
      .deinit = nullptr,
      .reset = xinput_reset,
      .open = xinput_open,
      .control_xfer_cb = xinput_control_xfer_cb,
      .xfer_cb = xinput_xfer_cb,
      .xfer_isr = nullptr,
      .sof = NULL};
}

bool XInputDriver::process(void *gamepad) {
  ThetaGP::Gamepad::Gamepad *gp =
      reinterpret_cast<ThetaGP::Gamepad::Gamepad *>(gamepad);
  const ThetaGP::Gamepad::GamepadRawInput &state = gp->getState();

  report.buttons_1 = (gp->pressedUp() ? XINPUT_MASK_UP : 0) |
                     (gp->pressedDown() ? XINPUT_MASK_DOWN : 0) |
                     (gp->pressedLeft() ? XINPUT_MASK_LEFT : 0) |
                     (gp->pressedRight() ? XINPUT_MASK_RIGHT : 0) |
                     (gp->pressedS2() ? XINPUT_MASK_START : 0) |
                     (gp->pressedS1() ? XINPUT_MASK_BACK : 0) |
                     (gp->pressedL3() ? XINPUT_MASK_LS : 0) |
                     (gp->pressedR3() ? XINPUT_MASK_RS : 0);

  report.buttons_2 = (gp->pressedL1() ? XINPUT_MASK_LB : 0) |
                     (gp->pressedR1() ? XINPUT_MASK_RB : 0) |
                     (gp->pressedA1() ? XINPUT_MASK_GUIDE : 0) |
                     (gp->pressedB1() ? XINPUT_MASK_A : 0) |
                     (gp->pressedB2() ? XINPUT_MASK_B : 0) |
                     (gp->pressedB3() ? XINPUT_MASK_X : 0) |
                     (gp->pressedB4() ? XINPUT_MASK_Y : 0);

  report.left_trigger = gp->pressedL2() ? 0xFF : state.lt;
  report.right_trigger = gp->pressedR2() ? 0xFF : state.rt;

  // The state's sticks run from 0 to 65535 with the middle at 32767; this
  // protocol runs from -32768 to 32767 with the middle at zero, and reads the
  // vertical axes with up as the positive end.
  report.left_stick_x =
      static_cast<int16_t>(static_cast<int32_t>(state.lx) - 0x8000);
  report.left_stick_y =
      static_cast<int16_t>(0x7FFF - static_cast<int32_t>(state.ly));
  report.right_stick_x =
      static_cast<int16_t>(static_cast<int32_t>(state.rx) - 0x8000);
  report.right_stick_y =
      static_cast<int16_t>(0x7FFF - static_cast<int32_t>(state.ry));

  if (tud_suspended()) {
    tud_remote_wakeup();
  }

  if (s_endpoint_in == 0 || !tud_ready()) {
    _sent = false;
    return false;
  }
  // A report goes out on change, and every kIdleReportMs otherwise.
  const bool changed = std::memcmp(&report, last_report, sizeof(report)) != 0;
  const uint32_t now = Drivers::Device::SystemTimer::getInstance().getMillis();
  if (_sent && !changed && (now - _lastSent) < kIdleReportMs) {
    return false;
  }
  if (!usbd_edpt_claim(THETAGP_USB_RHPORT, s_endpoint_in)) {
    return false;
  }
  std::memcpy(s_report_buffer, &report, sizeof(report));
  if (!usbd_edpt_xfer(THETAGP_USB_RHPORT, s_endpoint_in, s_report_buffer,
                      sizeof(report), false)) {
    usbd_edpt_release(THETAGP_USB_RHPORT, s_endpoint_in);
    return false;
  }
  usbd_edpt_release(THETAGP_USB_RHPORT, s_endpoint_in);
  std::memcpy(last_report, &report, sizeof(report));
  _lastSent = now;
  _sent = true;
  return true;
}

uint16_t XInputDriver::get_report(uint8_t report_id, hid_report_type_t report_type,
                                 uint8_t *buffer, uint16_t reqlen) {
  (void)report_id;
  (void)report_type;
  uint16_t size = static_cast<uint16_t>(sizeof(report));
  if (size > reqlen) {
    size = reqlen;
  }
  std::memcpy(buffer, &report, size);
  return size;
}

void XInputDriver::set_report(uint8_t report_id, hid_report_type_t report_type,
                              uint8_t const *buffer, uint16_t bufsize) {
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)bufsize;
}

bool XInputDriver::vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                          tusb_control_request_t const *request) {
  (void)rhport;
  (void)stage;
  (void)request;
  return false;
}

const uint16_t *XInputDriver::get_descriptor_string_cb(uint8_t index,
                                                       uint16_t langid) {
  (void)langid;
  constexpr uint16_t kStringCount =
      sizeof(xinput_string_descriptors) / sizeof(xinput_string_descriptors[0]);
  if (index >= kStringCount) {
    return nullptr;
  }
  char *value = (char *)xinput_string_descriptors[index];
  return getStringDescriptor(value, index);
}

const uint8_t *XInputDriver::get_descriptor_device_cb() {
  return xinput_device_descriptor;
}

const uint8_t *XInputDriver::get_hid_descriptor_report_cb(uint8_t itf) {
  (void)itf;
  return nullptr;
}

const uint8_t *XInputDriver::get_interface_descriptor() {
  return xinput_interface_descriptor;
}

uint16_t XInputDriver::get_interface_descriptor_size() {
  return sizeof(xinput_interface_descriptor);
}

const uint8_t *XInputDriver::get_descriptor_device_qualifier_cb() {
  return nullptr;
}

uint16_t XInputDriver::GetJoystickMidValue() { return 0; }

} // namespace ThetaGP::Drivers::GPEmulator
