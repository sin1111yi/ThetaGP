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

#include "drivers/device/device.h"

#include <array>
#include <cstddef>

namespace ThetaGP::Drivers::Device {

class DeviceManager {
private:
  static constexpr size_t MAX_DEVICES = 16;
  std::array<Device *, MAX_DEVICES> _devices{};
  size_t _count = 0;

public:
  DeviceManager();

  static DeviceManager &getInstance() {
    static DeviceManager instance;
    return instance;
  }

  void registerDevice(Device *device);
  void initDevices();

  Device *findDevice(DeviceType type, uint8_t instanceId) const;
  Device *getDevice(size_t index) const;
  size_t getDeviceCount() const { return _count; }

  template <typename T>
  T *findDeviceTyped(DeviceType type, uint8_t instanceId) const {
    Device *device = findDevice(type, instanceId);
    return device ? static_cast<T *>(device) : nullptr;
  }
};

} // namespace ThetaGP::Drivers::Device
