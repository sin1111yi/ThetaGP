#pragma once

#include "drivers/device/devicemgr.h"
#include "drivers/gpdriver/gpdrivermanager.h"
#include "drivers/peripherals/peripheralsmgr.h"

#include "gamepad/gamepad.h"
#include "gamepad/scheduler/scheduler.h"

namespace ThetaGP {

class Core {
private:
  Gamepad::Gamepad &gamepad;
  Gamepad::Scheduler &scheduler;
  Drivers::Peripheral::PeripheralsManager &peripheralsManager;
  Drivers::GPDriver::GPDriverManager &gpDriverManager;
  Drivers::Device::DeviceManager &deviceManager;

public:
  Core();

  void Setup();
  void Bootup();
};
} // namespace ThetaGP