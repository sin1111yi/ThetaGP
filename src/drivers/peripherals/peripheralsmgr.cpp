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

#include "drivers/peripherals/bus/bus.h"
#include "drivers/peripherals/peripheralsmgr.h"
#include "drivers/peripherals/systick.h"
#include "drivers/peripherals/timer.h"
#include "drivers/peripherals/usbhw.h"

#include "BoardConfig.h"

#include "utils/log/log.h"

using namespace ThetaGP::Drivers::Peripheral;
using namespace ThetaGP::Drivers::Peripheral::BUS;
using namespace ThetaGP::Drivers::Peripheral::GPIO;

// Static desc tables from BoardConfig.h DESC_DATA macros
#if defined(USE_SPI_COUNT) && USE_SPI_COUNT > 0
static constexpr BUS::SpiDesc g_spiDescTable[USE_SPI_COUNT] = {
    SPI_DESC_DATA
};
#endif

#if defined(USE_UART_COUNT) && USE_UART_COUNT > 0
static constexpr BUS::UartDesc g_uartDescTable[USE_UART_COUNT] = {
    UART_DESC_DATA
};
#endif

PeripheralsManager::PeripheralsManager() {}

TIMER::Instance PeripheralsManager::reservedTimer(void) {
#if defined(STM32H7)
  return TIMER::Instance::Timer5;
#else
  return TIMER::Instance::TimerNone;
#endif
}

void PeripheralsManager::initPeripherals() {
  cycleCounterInit();

  Drivers::Peripheral::NVIC_EXTI::NvicExti::preinit();

  // SPI buses
  initSpiBuses();

  // UART buses
  initUartBuses();

  // USB (existing logic unchanged)
  USB::USBSpeed usbSpeed =
#if defined(USBHW_SPEED_HS)
      USB::USBSpeed::UsbHighSpeed;
#else
      USB::USBSpeed::UsbFullSpeed;
#endif

  USB::USBPeripheral usbPeriph =
#if defined(USBHW_IF_OTG1)
      USB::USBPeripheral::OTG1;
#elif defined(USBHW_IF_OTG2)
      USB::USBPeripheral::OTG2;
#elif defined(USBHW_IF_ULPI)
      USB::USBPeripheral::ULPI;
#else
#error "USB peripheral type not defined (USBHW_IF_OTG1, USBHW_IF_OTG2, or USBHW_IF_ULPI)"
#endif

  USB::HardwareUSB hwusb(usbSpeed, usbPeriph);
  hwusb.init();
}

void PeripheralsManager::initSpiBuses() {
#if defined(USE_SPI_COUNT) && USE_SPI_COUNT > 0
  for (int i = 0; i < USE_SPI_COUNT; i++) {
    new (&_spiBuses[i]) BUS::SpiBus(g_spiDescTable[i]);
  }
  // Buses are constructed with buffer pointers = nullptr.
  // Device init() must alloc via MempoolManager + setBuffers + bus.init().
#endif
}

void PeripheralsManager::initUartBuses() {
#if defined(USE_UART_COUNT) && USE_UART_COUNT > 0
  for (int i = 0; i < USE_UART_COUNT; i++) {
    new (&_uartBuses[i]) BUS::UartBus(g_uartDescTable[i]);
  }
#endif
}

BUS::SpiBus &PeripheralsManager::spiBus(int idx) {
  return _spiBuses[idx];
}

BUS::UartBus &PeripheralsManager::uartBus(int idx) {
  return _uartBuses[idx];
}
