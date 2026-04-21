#include "BoardConfig.h"

#include "drivers/peripherals/peripheralsmgr.h"
#include "drivers/peripherals/timer.h"
#include "drivers/peripherals/usbhw.h"

using namespace ThetaGP::Drivers::Peripheral;

PeripheralsManager::PeripheralsManager() {}

TIMER::Instance PeripheralsManager::reservedTimer(void)
{
#if defined(STM32H7)
  return TIMER::Instance::Timer5;
#else
  return TIMER::Instance::TimerNone;
#endif
}

void PeripheralsManager::init() {
  USB::HardwareUSB usb(USB::USBSpeed::UsbHighSpeedExternalPHY,
                       USB::USBPeripheral::ULPI);
  usb.init();
}