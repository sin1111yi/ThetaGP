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

namespace ThetaGP::Drivers::Device {

class Device {
protected:
  bool _initialized = false;
  const char *_name;

  Device(const char *name) : _name(name) {}

public:
  virtual ~Device() = default;

  virtual void init() = 0;

  [[nodiscard]] bool isInitialized() const { return _initialized; }
  [[nodiscard]] const char *getName() const { return _name; }

  void setInitialized(bool initialized) { _initialized = initialized; }
};

} // namespace ThetaGP::Drivers::Device
