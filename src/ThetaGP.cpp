/**
 * This file is a part of ThetaGP.
 *
 * ThetaGP is free software: you can redistribute it
 * and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * ThetaGP is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "BoardConfig.h"

#include "gamepad/gamepad.h"
#include "gamepad/scheduler/scheduler.h"

#include "drivers/device/devicemgr.h"
#include "drivers/device/keypad.h"
#include "drivers/device/system_timer.h"
#include "drivers/gpdriver/gpdrivermgr.h"

#include "drivers/peripherals/systick.h"

#include "tusb.h"

#include "ThetaGP.h"

using namespace ThetaGP;

using Device = Drivers::Device::Device;

ThetaGPManger::ThetaGPManger()
    : gamepad(Gamepad::Gamepad::getInstance()),
      scheduler(Gamepad::Scheduler::getInstance()),
      peripheralsManager(
          Drivers::Peripheral::PeripheralsManager::getInstance()),
      gpDriverManager(Drivers::GPDriver::GPDriverManager::getInstance()),
      deviceManager(Drivers::Device::DeviceManager::getInstance()) {}

void ThetaGPManger::Setup() {
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

void ThetaGPManger::Bootup() {
  // scheduler.tasksInit();

  // while (1) {
  //   scheduler.run();
  // }

  while (1) {
    gamepad.process();
    Drivers::GPDriver::GPDriverManager::getInstance()
        .getgpdriverDevice()
        ->process(&gamepad);
    tud_task();
    delay_ms(10);
  }
}

int main(void) {
  ThetaGP::ThetaGPManger thetaGPMgr;

  thetaGPMgr.Setup();
  thetaGPMgr.Bootup();
}
