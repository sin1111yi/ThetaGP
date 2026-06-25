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

#pragma once

#include <cstdint>

namespace ThetaGP::Drivers::Device::DisplayDrv {

/// Abstract display chip driver.
struct DisplayDriver {
  virtual ~DisplayDriver() = default;

  /// Hardware init sequence (reset, register config, power on).
  virtual void init() = 0;

  /// Set the write window for subsequent pixel writes.
  virtual void setWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) = 0;

  /// Write pixel data to the current window.
  virtual void writePixels(const uint8_t *data, uint32_t len) = 0;

  /// Screen dimensions in pixels.
  virtual uint16_t width() const = 0;
  virtual uint16_t height() const = 0;
};

} // namespace ThetaGP::Drivers::Device::DisplayDrv
