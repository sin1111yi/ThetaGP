#pragma once

#include "drivers/gpdriver/gpdriver.h"

namespace ThetaGP::Drivers::Device {

enum class InputMode : uint8_t { Config = 0, HID, Count };

class GPDriverManager {
public:
  GPDriverManager(GPDriverManager const &) = delete;
  void operator=(GPDriverManager const &) = delete;

  static GPDriverManager &getInstance() {
    static GPDriverManager instance;
    return instance;
  }

  GPDriver *getgpdriverDevice() { return usbdevice; }
  void setup(InputMode mode);
  InputMode getInputMode() { return inputMode; }
  bool isConfigMode() { return (inputMode == InputMode::Config); }

private:
  GPDriverManager() {}
  GPDriver *usbdevice;
  InputMode inputMode;
};

} // namespace ThetaGP::Drivers::Device
