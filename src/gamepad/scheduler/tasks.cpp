#include "drivers/gpdriver/gpdriver.h"
#include "drivers/gpdriver/gpdrivermgr.h"

#include "gamepad/gamepad.h"
#include "gamepad/scheduler/scheduler.h"

#include "tusb.h"

#include "ThetaGP.h"

using namespace ThetaGP;

#define ThetaGP ThetaGPManger::getInstance()

void Gamepad::Scheduler::taskMain(uint32_t currentTimeUs) {
  (void)currentTimeUs;
  return;
}

void Gamepad::Scheduler::taskGamepadCore(uint32_t currentTimeUs) {
  (void)currentTimeUs;

  Gamepad::getInstance().process();
  Drivers::GPDriver::GPDriverManager::getInstance().getgpdriverDevice()->process(&Gamepad::getInstance());
  tud_task();
}
