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

#include "drivers/device/logger.h"
#include "drivers/peripherals/peripheralsmgr.h"
#include "gamepad/scheduler/scheduler.h"
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

ThetaGamepad::ThetaGamepad() {}

void ThetaGamepad::setup() {
  // initialize Mempool Manager
  Mempool::MempoolManager::init();

  // setup peripherals' driver
  Drivers::Peripheral::PeripheralsManager::getInstance().initPeripherals();

  // setup devices' driver
  Drivers::Device::DeviceManager::getInstance().registerDevice(
      &Drivers::Device::Keypad::getInstance());
  Drivers::Device::DeviceManager::getInstance().registerDevice(
      &Drivers::Device::SystemTimer::getInstance());
  Drivers::Device::DeviceManager::getInstance().registerDevice(
      &Drivers::Device::Logger::getInstance());

  Drivers::Device::DeviceManager::getInstance().initDevices();

  LogInit(Drivers::Device::Logger::LoggerTransmitBytes);

  // setup GP drivers
  Drivers::GPDriver::GPDriverManager::getInstance().setup(
      Drivers::GPDriver::InputMode::HID);

  // setup gamepad
  Gamepad::Gamepad::getInstance().setup();
  Gamepad::Gamepad::getInstance().registerKeypadDevice(
      &Drivers::Device::Keypad::getInstance());
  Gamepad::Gamepad::getInstance().setButtonMappings();
}

void ThetaGamepad::bootup() {
  Gamepad::TaskManager::init();
  Gamepad::TaskManager::setupSysTasks();
  registerTasks();
  Gamepad::TaskManager::setupScheduler();

  while (1) {
    Gamepad::TaskManager::run();
  }
}

int main(void) {
  ThetaGP::ThetaGamepad::getInstance().setup();
  ThetaGP::ThetaGamepad::getInstance().bootup();
}
