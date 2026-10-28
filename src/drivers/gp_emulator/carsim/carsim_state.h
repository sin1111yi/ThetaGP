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

#pragma once

#include "drivers/device/analog/analog_input.h"

#include <cstdint>

namespace ThetaGP::Drivers::GPEmulator {

// The axes a car simulator reports, in the order its report names them and in
// the order a board's analog channels are read as.
enum class CarSimAxis : uint8_t {
  Steer = 0,  // The wheel's angle, its rest at the centre
  Throttle,   // The pedal's travel, its rest at the top
  Brake,      // The pedal's travel, its rest at the top
  Clutch,     // The pedal's travel, its rest at the top
  Handbrake,  // The lever's travel, its rest at the off end
  Count,
};

// The wheel as one report tick leaves it: the counts of the axes the board
// wires, before any calibration, and the gear the shifter stands in.
struct CarSimState {
  static constexpr uint8_t kAxisCount = static_cast<uint8_t>(CarSimAxis::Count);

  // A board that wires no shifter stands in no gear.
  static constexpr int8_t kGearNone = INT8_MIN;

  // Neutral, then the forward gears counting up from it, then reverse.
  static constexpr int8_t kGearNeutral = 0;
  static constexpr int8_t kGearReverse = -1;

  // The count each axis holds, in CarSimAxis order; an axis the board does not
  // wire keeps kAbsent.
  uint16_t count[kAxisCount] = {Device::AnalogInput::kAbsent,
                                Device::AnalogInput::kAbsent,
                                Device::AnalogInput::kAbsent,
                                Device::AnalogInput::kAbsent,
                                Device::AnalogInput::kAbsent};
  int8_t gear = kGearNone;

  // Takes the board's channels as the axes, in wiring order: channel 0 is the
  // steering, then the pedals and the handbrake, and an axis past the board's
  // channels reads as absent.
  void read(const uint16_t *channels, uint8_t channelCount) {
    for (uint8_t i = 0; i < kAxisCount; ++i) {
      count[i] = i < channelCount ? channels[i] : Device::AnalogInput::kAbsent;
    }
  }

  [[nodiscard]] bool present(CarSimAxis axis) const {
    return count[static_cast<uint8_t>(axis)] != Device::AnalogInput::kAbsent;
  }

  [[nodiscard]] bool hasGear() const { return gear != kGearNone; }
};

} // namespace ThetaGP::Drivers::GPEmulator
