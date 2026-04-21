#include "core.h"

#include "BoardConfig.h"

#include "gamepad/gamepad.h"
#include "gamepad/scheduler/scheduler.h"

#include "drivers/device/devicemgr.h"
#include "drivers/device/keypad.h"
#include "drivers/device/system_timer.h"
#include "drivers/gpdriver/gpdrivermanager.h"

#include "tusb.h"

using namespace ThetaGP;

Core::Core()
    : gamepad(Gamepad::Gamepad::getInstance()),
      scheduler(Gamepad::Scheduler::getInstance()),
      peripheralsManager(
          Drivers::Peripheral::PeripheralsManager::getInstance()),
      gpDriverManager(Drivers::GPDriver::GPDriverManager::getInstance()),
      deviceManager(Drivers::Device::DeviceManager::getInstance()) {}

void Core::Setup() {
  // setup peripherals' driver
  peripheralsManager.initPeripherals();

  // setup GP drivers
  gpDriverManager.setup(Drivers::GPDriver::InputMode::HID);
  gpDriverManager.getgpdriverDevice()->initialize();

  // TinyUSB initialize
  tusb_init(1);

  // setup devices' driver
  deviceManager.registerDevice(&Drivers::Device::Keypad::getInstance());
  deviceManager.registerDevice(&Drivers::Device::SystemTimer::getInstance());
  deviceManager.initDevices();

  // setup gamepad
  gamepad.setup();
  gamepad.registerKeypadDevice(reinterpret_cast<Drivers::Device::Device &>(
      Drivers::Device::Keypad::getInstance()));
  gamepad.setDefaultMappings();
}

void Core::Bootup() {
  scheduler.tasksInit();

  while (1) {
    scheduler.run();
  }
}