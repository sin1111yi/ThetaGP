#pragma once

#include "drivers/device/devicemgr.h"
#include "drivers/gpdriver/gpdrivermgr.h"
#include "drivers/peripherals/peripheralsmgr.h"

#include "gamepad/gamepad.h"
#include "taskmanager.h"

namespace ThetaGP {

class ThetaGamepad {
public:
  ThetaGamepad();
  static ThetaGamepad &getInstance() {
    static ThetaGamepad instance;
    return instance;
  }

  void setup();
  void registerTasks();
  void bootup();
};
} // namespace ThetaGP