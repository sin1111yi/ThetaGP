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

#include "utils/log/log.h"
#include "utils/mempool/mempoolmanager.h"

#include "gamepad/gamepad.h"
#include "taskmanager.h"

#include "drivers/device/devicemgr.h"
#include "drivers/device/keypad.h"
#include "drivers/device/systimer.h"
#include "drivers/gpdriver/gpdrivermgr.h"

#include "drivers/peripherals/systick.h"

#include "tusb.h"

#include "ThetaGP.h"

using namespace ThetaGP;

using Device = Drivers::Device::Device;

ThetaGamepad::ThetaGamepad()
    : gamepad(Gamepad::Gamepad::getInstance()),
      taskManager(Gamepad::TaskManager::getInstance()),
      peripheralsManager(
          Drivers::Peripheral::PeripheralsManager::getInstance()),
      gpDriverManager(Drivers::GPDriver::GPDriverManager::getInstance()),
      deviceManager(Drivers::Device::DeviceManager::getInstance()),
      mempoolManager(Mempool::MempoolManager::getInstance()) {}

void ThetaGamepad::Setup() {
  // initialize Mempool Manager
  mempoolManager.init();

  // setup peripherals' driver
  peripheralsManager.initPeripherals();

  // setup GP drivers
  gpDriverManager.setup(Drivers::GPDriver::InputMode::HID);

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

void ThetaGamepad::Bootup() {
  taskManager.init();

  while (1) {
    taskManager.run();
  }
}

int main(void) {
  ThetaGP::ThetaGamepad::getInstance().Setup();
  ThetaGP::ThetaGamepad::getInstance().Bootup();
}
