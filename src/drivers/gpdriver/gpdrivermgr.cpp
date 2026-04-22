#include "drivers/gpdriver/gpdrivermgr.h"
#include "drivers/gpdriver/hid/HIDDriver.h"

namespace ThetaGP::Drivers::GPDriver {

GPDriverManager::GPDriverManager()
    : usbdevice(nullptr), inputMode(InputMode::None) {}

void GPDriverManager::setup(InputMode mode) {
  switch (mode) {
  case InputMode::HID:
    usbdevice = new HIDDriver();
    break;
  default:
    return;
  }

  if (usbdevice != nullptr) {
    usbdevice->initialize();
  }
  inputMode = mode;
}

} // namespace ThetaGP::Drivers::GPDriver
