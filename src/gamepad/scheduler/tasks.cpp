#include "drivers/gpdriver/gpdriver.h"
#include "drivers/gpdriver/gpdrivermgr.h"

#include "gamepad/gamepad.h"
#include "gamepad/scheduler/scheduler.h"

#include "tusb.h"

using namespace ThetaGP::Gamepad;

void Scheduler::taskMain(uint32_t currentTimeUs) {
  (void)currentTimeUs;
  return;
}

void taskThetaGPInit(uint32_t currentTimeUs) {
  (void)currentTimeUs;
  return;
}

void Scheduler::taskGamepadCore(uint32_t currentTimeUs) {
  (void)currentTimeUs;

  Gamepad::getInstance().process();
  Drivers::GPDriver::GPDriverManager::getInstance().getgpdriverDevice()->process(&Gamepad::getInstance());
  tud_task();
}
