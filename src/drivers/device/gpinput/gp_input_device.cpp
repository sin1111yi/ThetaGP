#include "drivers/device/gpinput/gp_input_device.h"
#include "drivers/device/gpinput/hid/HIDDriver.h"

namespace ThetaGP::Drivers::Device {

void GPInputDevice::setup(InputMode mode) {
  switch (mode) {
  case InputMode::Config:
    usbdevice = new HIDDriver();
    break;
  default:
    return;
  }

  usbdevice->initialize();
  inputMode = mode;
}

}  // namespace ThetaGP::Drivers::Device
