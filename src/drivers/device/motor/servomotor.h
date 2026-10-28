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

// The motor that turns the wheel, as the force feedback layer sees it: a torque
// to hold, signed, and whether the motor is driven at all. The angle is not read
// here; it arrives through AnalogInput, which the same board wires.
class ServoMotor : public Device {
public:
  // The torque to hold, in thousandths of the motor's rated torque.
  virtual void applyTorque(int16_t permille) = 0;

  // Drives the motor, or leaves the wheel free.
  virtual void setEnabled(bool on) = 0;
  [[nodiscard]] virtual bool enabled() const = 0;

protected:
  explicit ServoMotor(const char *name) : Device(name) {}
};

} // namespace ThetaGP::Drivers::Device
