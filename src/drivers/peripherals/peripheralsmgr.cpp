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

#include "drivers/peripherals/bus/bus.h"
#include "drivers/peripherals/peripheralsmgr.h"
#include "drivers/peripherals/systick.h"
#include "drivers/peripherals/timer.h"
#include "drivers/peripherals/usbhw.h"

#include "utils/log/log.h"

using namespace ThetaGP::Drivers::Peripheral;

PeripheralsManager::PeripheralsManager() {}

TIMER::Instance PeripheralsManager::reservedTimer(void) {
#if defined(STM32H7)
  return TIMER::Instance::Timer5;
#else
  return TIMER::Instance::TimerNone;
#endif
}

BUS::DebugUartBus &PeripheralsManager::debugUart() {
  return BUS::DebugUartBus::getInstance();
}

static void print(uint8_t *data, uint16_t num) {
  if (PeripheralsManager::getInstance().debugUart().isInitialized())
    PeripheralsManager::getInstance().debugUart().write(data, num);
}

void PeripheralsManager::initPeripherals() {
  cycleCounterInit();

  Drivers::Peripheral::NVIC_EXTI::NvicExti::preinit();
  Drivers::Peripheral::BUS::BusMem::getInstance().init();

  USB::HardwareUSB hwusb(USB::USBSpeed::UsbHighSpeedExternalPHY,
                         USB::USBPeripheral::ULPI);
  hwusb.init();

  // TODO: debugging code, a way for initializing is needed.
  debugUart().init();
  LogInit(print);

  LOG_DEBUG("Peripherals Init Done!");
}