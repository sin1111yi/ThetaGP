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

namespace ThetaGP::Drivers::Device {

enum class DeviceType : uint8_t {
  None = 0,
  Keypad,
  SystemTimer,
  Led,
  Custom,
};

class Device {
protected:
  bool _initialized = false;
  DeviceType _type;
  uint8_t _instanceId;

  Device(DeviceType type, uint8_t instanceId = 0)
      : _type(type), _instanceId(instanceId) {}

public:
  virtual ~Device() = default;

  virtual void init() = 0;

  [[nodiscard]] bool isInitialized() const { return _initialized; }
  [[nodiscard]] DeviceType getType() const { return _type; }
  [[nodiscard]] uint8_t getInstanceId() const { return _instanceId; }

  void setInitialized(bool initialized) { _initialized = initialized; }
  void setInstanceId(uint8_t id) { _instanceId = id; }
};

} // namespace ThetaGP::Drivers::Device
