#ifndef _GP_INPUT_DEVICE_H_
#define _GP_INPUT_DEVICE_H_

#include "drivers/device/gpinput/gpdriver.h"
#include "drivers/device/gpinput/gp_input_enums.h"

namespace ThetaGP::Drivers::Device {

class GPInputDevice {
public:
  GPInputDevice(GPInputDevice const &) = delete;
  void operator=(GPInputDevice const &) = delete;

  static GPInputDevice &getInstance() {
    static GPInputDevice instance;
    return instance;
  }

  GPInputUSBDevice *getGPUsbDevice() { return usbdevice; }
  void setup(InputMode mode);
  InputMode getInputMode() { return inputMode; }
  bool isConfigMode() { return (inputMode == InputMode::Config); }

private:
  GPInputDevice() {}
  GPInputUSBDevice *usbdevice;
  InputMode inputMode;
};

}  // namespace ThetaGP::Drivers::Device

#endif
