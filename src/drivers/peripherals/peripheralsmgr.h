#pragma once

#include "drivers/peripherals/timer.h"

namespace ThetaGP::Drivers::Peripheral {

class PeripheralsManager {
public:
  PeripheralsManager();

  static PeripheralsManager &getInstance() {
    static PeripheralsManager instance;
    return instance;
  }

  void init();

  TIMER::Instance reservedTimer(void);
};

} // namespace ThetaGP::Drivers::Peripheral