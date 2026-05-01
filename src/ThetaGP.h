#pragma once

#include "drivers/device/devicemgr.h"
#include "drivers/gpdriver/gpdrivermgr.h"
#include "drivers/peripherals/peripheralsmgr.h"

#include "gamepad/gamepad.h"
#include "taskmanager.h"

namespace ThetaGP {

class ThetaGamepad {
  friend class Gamepad::TaskManager;

private:
  Gamepad::Gamepad *gamepad = nullptr;
  Drivers::Peripheral::PeripheralsManager *peripheralsManager = nullptr;
  Drivers::GPDriver::GPDriverManager *gpDriverManager = nullptr;
  Drivers::Device::DeviceManager *deviceManager = nullptr;

public:
  ThetaGamepad();
  static ThetaGamepad &getInstance() {
    static ThetaGamepad instance;
    return instance;
  }

  void Setup();
  void Bootup();
};
} // namespace ThetaGP