#pragma once

#include "drivers/peripherals/gpio.h"

#include "build_info.h"

#include <cstdint>

namespace ThetaGP::Drivers::USB {

enum class USBSpeed : uint8_t {
  UsbFullSpeed,
  UsbHighSpeedInternalPHY,
  UsbHighSpeedExternalPHY,
};

enum class USBPeripheral : uint8_t {
  UsbDifferencePair, // difference pair direct to USB connector
  UsbULPI,           // ULPI Bus to a PHY, then PHY to USB connector
};

enum class ULPI : uint8_t {
  CLK,  // ULPI clock
  STP,  // ULPI strobe
  DIR,  // ULPI direction
  NXT,  // ULPI next
  D0,   // ULPI data bit 0
  D1,   // ULPI data bit 1
  D2,   // ULPI data bit 2
  D3,   // ULPI data bit 3
  D4,   // ULPI data bit 4
  D5,   // ULPI data bit 5
  D6,   // ULPI data bit 6
  D7,   // ULPI data bit 7
};

class Gpio : private GPIO::Gpio {
public:
  using GPIO::Gpio::config;
  using GPIO::Gpio::init;

  Gpio(const GPIO::PinDesc &pinDesc) : GPIO::Gpio(pinDesc) {}
};

class USB {
private:
  bool _initialized;
  USBSpeed _speed;
  USBPeripheral _peripheral;

public:
  USB(USBSpeed speed, USBPeripheral peripheral);

  void init(void);

  bool isInitialized() const { return _initialized; }
};

} // namespace ThetaGP::Drivers::USB
