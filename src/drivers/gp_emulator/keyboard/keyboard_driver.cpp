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
 * You should have received a copy of the GNU General
 * Public License along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "drivers/gp_emulator/keyboard/keyboard_driver.h"

#include "build_info.h"
#include "drivers/gp_emulator/shared/driver_helper.h"

#include "gamepad/gamepad.h"

#include "tusb.h"

#include <cstddef>
#include <cstring>

namespace ThetaGP::Drivers::GPEmulator {
namespace {

// The key each button bit sends, indexed by the bit the gamepad state carries:
// the four directions, then the four face buttons, the two shoulder pairs, the
// two system buttons, the stick clicks, the four auxiliaries and the eight
// extension buttons.
constexpr uint8_t kKeyOfButton[32] = {
    HID_KEY_ARROW_UP,     // up
    HID_KEY_ARROW_DOWN,   // down
    HID_KEY_ARROW_LEFT,   // left
    HID_KEY_ARROW_RIGHT,  // right
    HID_KEY_J,            // B1
    HID_KEY_K,            // B2
    HID_KEY_L,            // B3
    HID_KEY_I,            // B4
    HID_KEY_U,            // L1
    HID_KEY_O,            // R1
    HID_KEY_Y,            // L2
    HID_KEY_H,            // R2
    HID_KEY_ENTER,        // S1
    HID_KEY_BACKSPACE,    // S2
    HID_KEY_R,            // L3
    HID_KEY_F,            // R3
    HID_KEY_1,            // A1
    HID_KEY_2,            // A2
    HID_KEY_3,            // A3
    HID_KEY_4,            // A4
    HID_KEY_ARROW_UP,     // DU
    HID_KEY_ARROW_DOWN,   // DD
    HID_KEY_ARROW_LEFT,   // DL
    HID_KEY_ARROW_RIGHT,  // DR
    HID_KEY_5,            // E1
    HID_KEY_6,            // E2
    HID_KEY_7,            // E3
    HID_KEY_8,            // E4
    HID_KEY_9,            // E5
    HID_KEY_0,            // E6
    HID_KEY_MINUS,        // E7
    HID_KEY_EQUAL,        // E8
};

bool keyboard_control_xfer_cb(uint8_t rhport, uint8_t stage,
                              tusb_control_request_t const *request) {
  return hidd_control_xfer_cb(rhport, stage, request);
}

} // namespace

void KeyboardDriver::initialize() {
  _report = {};
  _lastReport = _report;

  // Derive USB product ID from board name (hash → lower 16 bits)
  uint16_t pid = static_cast<uint16_t>(
      GPEmulator::get_string_hash_u32(BOARD_NAME) & 0xFFFF);
  keyboard_device_descriptor[10] = pid & 0xFF;
  keyboard_device_descriptor[11] = (pid >> 8) & 0xFF;

  class_driver = {
#if CFG_TUSB_DEBUG >= 2
      .name = "Keyboard",
#endif
      .init = hidd_init,
      .deinit = nullptr,
      .reset = hidd_reset,
      .open = hidd_open,
      .control_xfer_cb = keyboard_control_xfer_cb,
      .xfer_cb = hidd_xfer_cb,
      .xfer_isr = nullptr,
      .sof = NULL};
}

bool KeyboardDriver::process(void *gamepad) {
  ThetaGP::Gamepad::Gamepad *gp =
      static_cast<ThetaGP::Gamepad::Gamepad *>(gamepad);

  // The directions come from the cleaned D-pad, so the cleaning the profile asks
  // for reaches the keyboard too; every other key comes from the button mask.
  KeyboardReport next{};
  uint8_t held = 0;
  const uint32_t directions = (gp->getState().dpad & GAMEPAD_MASK_DPAD);
  for (uint8_t bit = 0; bit < 32 && held < KEYBOARD_KEYS; ++bit) {
    const bool pressed = bit < 4 ? (directions & (1UL << bit)) != 0
                                 : (gp->getState().buttons & (1UL << bit)) != 0;
    if (!pressed) {
      continue;
    }
    const uint8_t key = kKeyOfButton[bit];
    if (key != 0) {
      next.keys[held++] = key;
    }
  }

  if (memcmp(&_lastReport, &next, sizeof(next)) != 0) {
    if (tud_hid_ready() && tud_hid_report(0, &next, sizeof(next))) {
      _lastReport = next;
      _report = next;
      return true;
    }
  }

  return false;
}

uint16_t KeyboardDriver::get_report(uint8_t report_id, hid_report_type_t report_type,
                                    uint8_t *buffer, uint16_t reqlen) {
  UNUSED(report_id);
  UNUSED(report_type);
  // The report is answered in as much of itself as the caller asked for.
  uint16_t size = static_cast<uint16_t>(sizeof(_report));
  if (size > reqlen) {
    size = reqlen;
  }
  memcpy(buffer, &_report, size);
  return size;
}

void KeyboardDriver::set_report(uint8_t report_id, hid_report_type_t report_type,
                                uint8_t const *buffer, uint16_t bufsize) {
  // The host writes the LEDs here, and the board carries none.
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)bufsize;
}

bool KeyboardDriver::vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                            tusb_control_request_t const *request) {
  (void)rhport;
  (void)stage;
  (void)request;
  return false;
}

const uint16_t *KeyboardDriver::get_descriptor_string_cb(uint8_t index,
                                                         uint16_t langid) {
  (void)langid;
  constexpr uint16_t kStringCount =
      sizeof(keyboard_string_descriptors) /
      sizeof(keyboard_string_descriptors[0]);
  // A host may name an index this set does not carry: past the last entry there
  // is no string to read.
  if (index >= kStringCount) {
    return nullptr;
  }
  char *value = (char *)keyboard_string_descriptors[index];
  return getStringDescriptor(value, index);
}

const uint8_t *KeyboardDriver::get_descriptor_device_cb() {
  return (const uint8_t *)&keyboard_device_descriptor;
}

const uint8_t *KeyboardDriver::get_hid_descriptor_report_cb(uint8_t itf) {
  (void)itf;
  return keyboard_report_descriptor;
}

const uint8_t *KeyboardDriver::get_interface_descriptor() {
  return keyboard_interface_descriptor;
}

uint16_t KeyboardDriver::get_interface_descriptor_size() {
  return sizeof(keyboard_interface_descriptor);
}

const uint8_t *KeyboardDriver::get_descriptor_device_qualifier_cb() {
  return nullptr;
}

uint16_t KeyboardDriver::GetJoystickMidValue() { return 0x8000; }

} // namespace ThetaGP::Drivers::GPEmulator
