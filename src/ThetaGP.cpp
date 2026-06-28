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
#include "drivers/device/devmem.h"
#include "drivers/peripherals/peripheralsmgr.h"
#include "gamepad/scheduler/scheduler.h"
#include "utils/log/log.h"
#include "utils/mempool/mempoolmanager.h"

#include "gamepad/gamepad.h"
#include "gamepad/config/configmgr.h"
#include "taskmanager.h"

#include "drivers/device/devicemgr.h"
#include "drivers/device/flash/flash_base.h"
#include "drivers/device/keypad.h"
#include "drivers/device/run_led.h"
#include "drivers/device/systimer.h"
#ifdef USE_DISPLAY
#include "drivers/device/display/display.h"
#endif
#include "drivers/gpdriver/gpdrivermgr.h"

#include "drivers/peripherals/systick.h"

#include "tusb.h"

#include "ThetaGP.h"

#include "test/init.h"

using namespace ThetaGP;

ThetaGamepad::ThetaGamepad() {}

void ThetaGamepad::setup() {
  // initialize Mempool Manager
  Mempool::MempoolManager::init();

  // initialize Device memory pool (AXI SRAM)
  (void)Drivers::Device::DevMem::getInstance().init();

  // setup peripherals' driver
  Drivers::Peripheral::PeripheralsManager::getInstance().initPeripherals();

  // setup devices' driver
  (void)Drivers::Device::DeviceManager::getInstance().registerDevice(
      &Drivers::Device::Keypad::getInstance());
  (void)Drivers::Device::DeviceManager::getInstance().registerDevice(
      &Drivers::Device::SystemTimer::getInstance());
  (void)Drivers::Device::DeviceManager::getInstance().registerDevice(
      &Drivers::Device::RunLed::getInstance());
#ifdef USE_DISPLAY
  (void)Drivers::Device::DeviceManager::getInstance().registerDevice(
      &Drivers::Device::Display::getInstance());
#endif
#ifdef LOGGER_UART
  (void)Drivers::Device::DeviceManager::getInstance().registerDevice(
      &Drivers::Device::Logger::getInstance());
#endif
  (void)Drivers::Device::DeviceManager::getInstance().registerDevice(
      &Drivers::Device::FlashBase::getInstance());

  Drivers::Device::DeviceManager::getInstance().initDevices();

  Drivers::Device::RunLed::getInstance().setEffect(
      Drivers::Device::RunLed::Effect::DoubleFlash);
  // setup GP drivers
  Drivers::GPDriver::GPDriverManager::getInstance().setup(
      Drivers::GPDriver::InputMode::HID);

  // setup gamepad
  Gamepad::Gamepad::getInstance().setup();
  Gamepad::Gamepad::getInstance().registerKeypadDevice(
      &Drivers::Device::Keypad::getInstance());
  Gamepad::Gamepad::getInstance().setButtonMappings();

  // initialize configuration system (ProfileStore + ConfigManager)
  Gamepad::Config::ConfigManager::getInstance().init();

  ThetaGP::Test::initTestSystem();
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
