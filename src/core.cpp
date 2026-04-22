#include "core.h"

#include "BoardConfig.h"

#include "gamepad/gamepad.h"
#include "gamepad/scheduler/scheduler.h"

#include "drivers/device/devicemgr.h"
#include "drivers/device/keypad.h"
#include "drivers/device/system_timer.h"
#include "drivers/gpdriver/gpdrivermgr.h"

#include "tusb.h"

using namespace ThetaGP;

using Device = Drivers::Device::Device;

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

  // TinyUSB initialize
  tusb_init(1);

  // setup devices' driver
  deviceManager.registerDevice(
      reinterpret_cast<Device *>(&Drivers::Device::Keypad::getInstance()));
  deviceManager.registerDevice(
      reinterpret_cast<Device *>(&Drivers::Device::SystemTimer::getInstance()));
  deviceManager.initDevices();

  // setup gamepad
  gamepad.setup();
  gamepad.registerKeypadDevice(
      reinterpret_cast<Device &>(Drivers::Device::Keypad::getInstance()));
  gamepad.setDefaultMappings();
}

void Core::Bootup() {
  scheduler.tasksInit();

  while (1) {
    scheduler.run();
  }
}