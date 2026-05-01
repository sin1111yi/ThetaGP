
#include "device/usbd.h"
#include "drivers/gpdriver/gpdrivermgr.h"
#include "gamepad/gamepad.h"
#include "utils/utils.h"

#include "taskmanager.h"

#include "ThetaGP.h"

#include <cstdint>

using namespace ThetaGP;
using namespace ThetaGP::Gamepad;

static void taskGamepadCore(uint32_t currentTimeUs) {
  UNUSED(currentTimeUs);

  static Gamepad::Gamepad &gamepad = Gamepad::Gamepad::getInstance();
  gamepad.process();
  Drivers::GPDriver::GPDriverManager::getInstance()
      .getgpdriverDevice()
      ->process(&gamepad);
  tud_task();
}

void ThetaGP::ThetaGamepad::registerTasks(void) {
  TaskManager::registerTask("GAMEPAD", "CORE", taskGamepadCore,
                            TASK_PERIOD_HZ(1000), TaskPriority::Realtime);
}