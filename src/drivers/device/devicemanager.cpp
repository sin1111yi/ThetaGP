#include "drivers/device/general_device.h"

namespace ThetaGP::Drivers::Device {

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

void DeviceManager::initAll() {
  for (size_t i = 0; i < _count; i++) {
    if (_devices[i] && !_devices[i]->isInitialized()) {
      _devices[i]->init();
    }
  }
}

void DeviceManager::initByType(DeviceType type) {
  for (size_t i = 0; i < _count; i++) {
    if (_devices[i] && _devices[i]->getType() == type) {
      if (!_devices[i]->isInitialized()) {
        _devices[i]->init();
      }
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

}  // namespace ThetaGP::Drivers::Device
