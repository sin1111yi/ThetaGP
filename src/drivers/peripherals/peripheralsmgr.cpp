#include "BoardConfig.h"

#include "drivers/peripherals/peripheralsmgr.h"
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

void PeripheralsManager::initPeripherals() {
  // set NVIC priority grouping
  Drivers::Peripheral::NVIC_EXTI::NvicExti::preinit();

  USB::HardwareUSB usb(USB::USBSpeed::UsbHighSpeedExternalPHY,
                       USB::USBPeripheral::ULPI);

  usb.init();
}