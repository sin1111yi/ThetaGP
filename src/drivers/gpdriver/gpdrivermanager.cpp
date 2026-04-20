#include "drivers/gpdriver/gpdrivermanager.h"
#include "drivers/gpdriver/hid/HIDDriver.h"

namespace ThetaGP::Drivers::Device {

void GPDriverManager::setup(InputMode mode) {
  switch (mode) {
  case InputMode::HID:
    usbdevice = new HIDDriver();
    break;
  default:
    return;
  }

  usbdevice->initialize();
  inputMode = mode;
}

}  // namespace ThetaGP::Drivers::Device
