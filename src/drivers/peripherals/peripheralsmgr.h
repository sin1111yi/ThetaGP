#pragma once

#include "drivers/peripherals/bus/bus_uart.h"
#include "drivers/peripherals/timer.h"

namespace ThetaGP::Drivers::Peripheral {

class PeripheralsManager {
public:
  PeripheralsManager();

  static PeripheralsManager &getInstance() {
    static PeripheralsManager instance;
    return instance;
  }

  void initPeripherals();

  TIMER::Instance reservedTimer();
  BUS::DebugUartBus& debugUart();
};

} // namespace ThetaGP::Drivers::Peripheral