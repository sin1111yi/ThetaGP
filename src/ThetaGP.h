#pragma once

#include "drivers/device/devicemgr.h"
#include "drivers/gpdriver/gpdrivermgr.h"
#include "drivers/peripherals/peripheralsmgr.h"
#include "utils/mempool/mempoolmanager.h"

#include "gamepad/gamepad.h"
#include "gamepad/scheduler/scheduler.h"

namespace ThetaGP {

class ThetaGamepad {
private:
  Gamepad::Gamepad &gamepad;
  Gamepad::Scheduler &scheduler;
  Drivers::Peripheral::PeripheralsManager &peripheralsManager;
  Drivers::GPDriver::GPDriverManager &gpDriverManager;
  Drivers::Device::DeviceManager &deviceManager;
  Mempool::MempoolManager& mempoolManager;

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