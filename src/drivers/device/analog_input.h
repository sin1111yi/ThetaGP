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

#include "drivers/device/device.h"

#include <cstdint>

namespace ThetaGP::Drivers::Device {

// The analog channels a wheel carries, in the order a report and a calibration
// row name them. A channel added goes at the end, so a value already written
// keeps its meaning.
enum class AnalogChannel : uint8_t {
  Steer = 0,  // The wheel's angle, its rest at the centre
  Throttle,   // The pedal's travel, its rest at the top
  Brake,      // The pedal's travel, its rest at the top
  Clutch,     // The pedal's travel, its rest at the top
  Handbrake,  // The lever's travel, its rest at the off end
  Count,
};

// What one read of the channels holds, before any calibration: the counts the
// sensor produced, and the gear the shifter stands in. The counts are the
// sensor's own range, so the layer that maps a frame to a report is the one that
// scales them.
struct AnalogFrame {
  static constexpr uint8_t kChannelCount =
      static_cast<uint8_t>(AnalogChannel::Count);

  // The count of a channel the board does not wire: a report leaves it out
  // rather than presenting a value nothing measured.
  static constexpr uint16_t kAbsent = 0xFFFF;

  // A board that wires no shifter reports this gear.
  static constexpr int8_t kGearNone = INT8_MIN;

  // Neutral, then the forward gears counting up from it, then reverse.
  static constexpr int8_t kGearNeutral = 0;
  static constexpr int8_t kGearReverse = -1;

  uint16_t count[kChannelCount] = {kAbsent, kAbsent, kAbsent,
                                   kAbsent, kAbsent};
  int8_t gear = kGearNone;

  [[nodiscard]] bool present(AnalogChannel channel) const {
    return count[static_cast<uint8_t>(channel)] != kAbsent;
  }
};

// The interface a board's analog acquisition implements: one read per report
// tick, the counts it took, and the gear the shifter stands in. A board without
// a sensor registers none, and every frame then reads as absent.
class AnalogInput : public Device {
public:
  // Fills `out` with the counts of this tick. A channel the board does not wire
  // is left at AnalogFrame::kAbsent.
  virtual void read(AnalogFrame &out) = 0;

protected:
  explicit AnalogInput(const char *name) : Device(name) {}
};

} // namespace ThetaGP::Drivers::Device
