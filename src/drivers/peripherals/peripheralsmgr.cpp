#include "BoardConfig.h"

#include "drivers/peripherals/peripheralsmgr.h"
#include "drivers/peripherals/usbhw.h"

using namespace ThetaGP::Drivers::Peripheral;

PeripheralsManager::PeripheralsManager() {}

void PeripheralsManager::init() {
  USB::HardwareUSB usb(USB::USBSpeed::UsbHighSpeedExternalPHY,
                       USB::USBPeripheral::ULPI);
  usb.init();
}