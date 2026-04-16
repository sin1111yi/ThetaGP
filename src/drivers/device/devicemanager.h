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
  DeviceManager() = default;

  DeviceManager &getInstance() {
    static DeviceManager instance;
    return instance;
  }

  void registerDevice(Device *device);
  void initAll();
  void initByType(DeviceType type);

  Device *findDevice(DeviceType type, uint8_t instanceId = 0) const;
  Device *getDevice(size_t index) const;
  size_t getDeviceCount() const { return _count; }

  template <typename T>
  T *findDeviceTyped(DeviceType type, uint8_t instanceId = 0) const {
    Device *device = findDevice(type, instanceId);
    return device ? static_cast<T *>(device) : nullptr;
  }
};

} // namespace ThetaGP::Drivers::Device
