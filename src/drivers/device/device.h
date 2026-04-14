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
