#pragma once

#include "drivers/device/device.h"
#include <cstdint>

namespace ThetaGP::Drivers::Device {

class SystemTimer : public Device {
public:
  static SystemTimer &getInstance() {
    static SystemTimer instance;
    return instance;
  }

  void init() override;

  uint32_t getMicros() const;
  uint32_t getMillis() const;
  uint32_t getCycleCounter() const;

  uint32_t microsToCycles(uint32_t micros) const;
  int32_t cyclesToMicros(int32_t cycles) const;
  float cyclesToMicrosf(int32_t cycles) const;

  void delayMicroseconds(uint32_t us) const;
  void delayMilliseconds(uint32_t ms) const;

private:
  SystemTimer();
};

} // namespace ThetaGP::Drivers::Device
