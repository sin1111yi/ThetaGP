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

#include "drivers/device/devicemgr.h"

namespace ThetaGP::Drivers::Device {

DeviceManager::DeviceManager() {}

void DeviceManager::registerDevice(Device *device) {
  if (!device || _count >= MAX_DEVICES) {
    return;
  }

  for (size_t i = 0; i < _count; i++) {
    if (_devices[i] == device) {
      return;
    }
  }

  uint8_t instanceId = 0;
  for (uint8_t id = 0; id < 255; id++) {
    bool used = false;
    for (size_t i = 0; i < _count; i++) {
      if (_devices[i]->getType() == device->getType() &&
          _devices[i]->getInstanceId() == id) {
        used = true;
        break;
      }
    }
    if (!used) {
      instanceId = id;
      break;
    }
  }

  device->setInstanceId(instanceId);
  _devices[_count++] = device;
}

void DeviceManager::initDevices() {
  for (size_t i = 0; i < _count; i++) {
    if (_devices[i] && !_devices[i]->isInitialized()) {
      _devices[i]->init();
    }
  }
}

Device *DeviceManager::findDevice(DeviceType type, uint8_t instanceId) const {
  for (size_t i = 0; i < _count; i++) {
    if (_devices[i] && _devices[i]->getType() == type &&
        _devices[i]->getInstanceId() == instanceId) {
      return _devices[i];
    }
  }
  return nullptr;
}

Device *DeviceManager::getDevice(size_t index) const {
  return (index < _count) ? _devices[index] : nullptr;
}

} // namespace ThetaGP::Drivers::Device
