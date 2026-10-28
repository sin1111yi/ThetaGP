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

// The interface a board's analog acquisition implements: how many channels it
// wires, and the counts it took on this tick. The channels carry no names here;
// what a count means to a report is the layer above's, and the counts arrive in
// the board's own wiring order.
class AnalogInput : public Device {
public:
  // The most channels a board may report.
  static constexpr uint8_t kChannelMax = 8;

  // The count of a channel the board does not wire, so a reader leaves it out
  // rather than presenting a value nothing measured.
  static constexpr uint16_t kAbsent = 0xFFFF;

  // The channels this board wires, never above kChannelMax.
  [[nodiscard]] virtual uint8_t channelCount() const = 0;

  // Reads this tick's counts into `counts`, which holds kChannelMax entries in
  // the board's wiring order.
  virtual void read(uint16_t *counts) = 0;

protected:
  explicit AnalogInput(const char *name) : Device(name) {}
};

} // namespace ThetaGP::Drivers::Device
