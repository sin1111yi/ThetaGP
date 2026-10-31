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

#include "drivers/gp_emulator/carsim/carsim_driver.h"

#include "conf/ThetaGP_Config.h"

#if THETAGP_CFG_CARSIM

#include "build_info.h"
#include "drivers/device/system_timer.h"
#include "drivers/gp_emulator/shared/driver_helper.h"

#include "gamepad/gamepad.h"

#include "tusb.h"

#include <cstddef>
#include <cstring>

namespace ThetaGP::Drivers::GPEmulator {
namespace {

// Where an axis rests: the steering at the centre of its range, the pedals and
// the lever at zero.
constexpr uint16_t kSteerRest = 0x8000;
constexpr uint16_t kPedalRest = 0;

// The hat a D-pad mask reports, indexed by the mask's four direction bits. A
// mask holding opposed directions reports no direction.
constexpr uint8_t kHatOfDpad[16] = {
    CARSIM_HAT_NOTHING,   // -
    CARSIM_HAT_UP,        // up
    CARSIM_HAT_DOWN,      // down
    CARSIM_HAT_NOTHING,   // up + down
    CARSIM_HAT_LEFT,      // left
    CARSIM_HAT_UPLEFT,    // up + left
    CARSIM_HAT_DOWNLEFT,  // down + left
    CARSIM_HAT_NOTHING,   // up + down + left
    CARSIM_HAT_RIGHT,     // right
    CARSIM_HAT_UPRIGHT,   // up + right
    CARSIM_HAT_DOWNRIGHT, // down + right
    CARSIM_HAT_NOTHING,   // up + down + right
    CARSIM_HAT_NOTHING,   // left + right
    CARSIM_HAT_NOTHING,   // up + left + right
    CARSIM_HAT_NOTHING,   // down + left + right
    CARSIM_HAT_NOTHING,   // every direction
};

// The count an axis reports, and the count it rests at when the board does not
// wire it.
uint16_t axisValue(const CarSimState &state, CarSimAxis axis, uint16_t rest) {
  const uint16_t raw = state.count[static_cast<uint8_t>(axis)];
  return raw == Device::AnalogInput::kAbsent ? rest : raw;
}

// The gear, in the four bits the report carries.
uint8_t gearValue(const CarSimState &state) {
  if (state.gear == CarSimState::kGearNone) {
    return CARSIM_GEAR_NONE;
  }
  if (state.gear == CarSimState::kGearReverse) {
    return CARSIM_GEAR_REVERSE;
  }
  if (state.gear < CarSimState::kGearNeutral || state.gear > 8) {
    return CARSIM_GEAR_NEUTRAL;
  }
  return static_cast<uint8_t>(state.gear);
}

bool carsim_control_xfer_cb(uint8_t rhport, uint8_t stage,
                            tusb_control_request_t const *request) {
  return hidd_control_xfer_cb(rhport, stage, request);
}

} // namespace

void CarSimDriver::initialize() {
  _report = {
      .buttons = 0,
      .direction = CARSIM_HAT_NOTHING,
      .gear = CARSIM_GEAR_NONE,
      .steer = kSteerRest,
      .throttle = kPedalRest,
      .brake = kPedalRest,
      .clutch = kPedalRest,
      .handbrake = kPedalRest,
  };
  _lastReport = _report;
  _effects.reset();
  _lastAngle = kSteerRest;

  // Derive USB product ID from board name (hash → lower 16 bits)
  uint16_t pid = static_cast<uint16_t>(
      GPEmulator::get_string_hash_u32(BOARD_NAME) & 0xFFFF);
  carsim_device_descriptor[10] = pid & 0xFF;
  carsim_device_descriptor[11] = (pid >> 8) & 0xFF;

  class_driver = {
#if CFG_TUSB_DEBUG >= 2
      .name = "CarSim",
#endif
      .init = hidd_init,
      .deinit = nullptr,
      .reset = hidd_reset,
      .open = hidd_open,
      .control_xfer_cb = carsim_control_xfer_cb,
      .xfer_cb = hidd_xfer_cb,
      .xfer_isr = nullptr,
      .sof = NULL};
}

bool CarSimDriver::process(void *gamepad) {
  ThetaGP::Gamepad::Gamepad *gp =
      static_cast<ThetaGP::Gamepad::Gamepad *>(gamepad);

  _state.read(gp->analogCounts(), gp->analogChannelCount());

  _report.buttons = gp->getState().buttons;
  _report.direction = kHatOfDpad[gp->getState().dpad & GAMEPAD_MASK_DPAD];
  _report.gear = gearValue(_state);
  _report.steer = axisValue(_state, CarSimAxis::Steer, kSteerRest);
  _report.throttle = axisValue(_state, CarSimAxis::Throttle, kPedalRest);
  _report.brake = axisValue(_state, CarSimAxis::Brake, kPedalRest);
  _report.clutch = axisValue(_state, CarSimAxis::Clutch, kPedalRest);
  _report.handbrake = axisValue(_state, CarSimAxis::Handbrake, kPedalRest);

  // The effects push against the angle and the speed the wheel moved at, and
  // the motor holds what they add up to. A board with no motor still resolves
  // them.
  const uint32_t nowUs = Drivers::Device::SystemTimer::getInstance().getMicros();
  const uint32_t nowMs =
      Drivers::Device::SystemTimer::getInstance().getMillis();
  const int16_t torque =
      _effects.torque(_report.steer, _lastAngle, nowUs - _lastUs, nowMs);
  _lastAngle = _report.steer;
  _lastUs = nowUs;

  if (Drivers::Device::ServoMotor *motor = gp->servoMotor(); motor != nullptr) {
    motor->applyTorque(torque);
  }

  // Wake up TinyUSB device only when state changes while suspended
  if (tud_suspended()) {
    if (memcmp(&_lastReport, &_report, sizeof(_report)) != 0) {
      tud_remote_wakeup();
    }
  }

  if (!tud_ready()) {
    _sent = false;
    return false;
  }
  if (!tud_hid_ready()) return false;

  // A report goes out on change, and every 100 ms otherwise.
  const bool changed = memcmp(&_lastReport, &_report, sizeof(_report)) != 0;
  if (_sent && !changed && (nowMs - _lastSent) < 100) {
    return false;
  }

  if (tud_hid_report(CARSIM_REPORT_AXES, &_report, sizeof(_report))) {
    memcpy(&_lastReport, &_report, sizeof(_report));
    _lastSent = nowMs;
    _sent = true;
    return true;
  }

  return false;
}

uint16_t CarSimDriver::get_report(uint8_t report_id, hid_report_type_t report_type,
                                  uint8_t *buffer, uint16_t reqlen) {
  if (report_type == HID_REPORT_TYPE_FEATURE) {
    return _effects.answer(report_id, buffer, reqlen);
  }

  // The axes report is answered in as much of itself as the caller asked for.
  uint16_t size = static_cast<uint16_t>(sizeof(_report));
  if (size > reqlen) {
    size = reqlen;
  }
  memcpy(buffer, &_report, size);
  return size;
}

void CarSimDriver::set_report(uint8_t report_id, hid_report_type_t report_type,
                              uint8_t const *buffer, uint16_t bufsize) {
  (void)report_type;
  _effects.handle(report_id, buffer, bufsize);
}

bool CarSimDriver::vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                          tusb_control_request_t const *request) {
  (void)rhport;
  (void)stage;
  (void)request;
  return false;
}

const uint16_t *CarSimDriver::get_descriptor_string_cb(uint8_t index,
                                                       uint16_t langid) {
  (void)langid;
  constexpr uint16_t kStringCount =
      sizeof(carsim_string_descriptors) / sizeof(carsim_string_descriptors[0]);
  // A host may name an index this set does not carry: past the last entry there
  // is no string to read.
  if (index >= kStringCount) {
    return nullptr;
  }
  char *value = (char *)carsim_string_descriptors[index];
  return getStringDescriptor(value, index);
}

const uint8_t *CarSimDriver::get_descriptor_device_cb() {
  return (const uint8_t *)&carsim_device_descriptor;
}

const uint8_t *CarSimDriver::get_hid_descriptor_report_cb(uint8_t itf) {
  (void)itf;
  return carsim_report_descriptor;
}

const uint8_t *CarSimDriver::get_interface_descriptor() {
  return carsim_interface_descriptor;
}

uint16_t CarSimDriver::get_interface_descriptor_size() {
  return sizeof(carsim_interface_descriptor);
}

const uint8_t *CarSimDriver::get_descriptor_device_qualifier_cb() {
  return nullptr;
}

uint16_t CarSimDriver::GetJoystickMidValue() { return kSteerRest; }

} // namespace ThetaGP::Drivers::GPEmulator

#endif // THETAGP_CFG_CARSIM
