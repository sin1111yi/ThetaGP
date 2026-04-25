#include "BoardConfig.h"

#include "drivers/peripherals/bus/bus.h"
#include "drivers/peripherals/peripheralsmgr.h"
#include "drivers/peripherals/systick.h"
#include "drivers/peripherals/timer.h"
#include "drivers/peripherals/usbhw.h"

using namespace ThetaGP::Drivers::Peripheral;

PeripheralsManager::PeripheralsManager() {}

TIMER::Instance PeripheralsManager::reservedTimer(void) {
#if defined(STM32H7)
  return TIMER::Instance::Timer5;
#else
  return TIMER::Instance::TimerNone;
#endif
}

BUS::DebugUartBus &PeripheralsManager::debugUart() {
  return BUS::DebugUartBus::getInstance();
}

void PeripheralsManager::initPeripherals() {
  cycleCounterInit();

  Drivers::Peripheral::NVIC_EXTI::NvicExti::preinit();
  Drivers::Peripheral::BUS::BusMem::getInstance().init();

  USB::HardwareUSB hwusb(USB::USBSpeed::UsbHighSpeedExternalPHY,
                         USB::USBPeripheral::ULPI);
  hwusb.init();

  debugUart().init();
  debugUart().write((uint8_t *)"Hello world!\r\n", 15);
}