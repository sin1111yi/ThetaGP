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
#include "drivers/peripherals/bus/bus_spi.h"
#include "drivers/peripherals/peripheralsmgr.h"
#include "drivers/peripherals/systick.h"
#include "drivers/peripherals/timer.h"
#include "drivers/peripherals/usbhw.h"

#include "BoardConfig.h"

#include <new>

#include "utils/log/log.h"

using namespace ThetaGP::Drivers::Peripheral;
using namespace ThetaGP::Drivers::Peripheral::BUS;
using namespace ThetaGP::Drivers::Peripheral::GPIO;

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
#error "[usb] hw_periph not configured — set USB1, USB2, or ULPI in BoardConfig.toml (see configs/CONFIGURATION.md)"
#endif

  USB::HardwareUSB hwusb(usbSpeed, usbPeriph);
  (void)hwusb.init();
}

void PeripheralsManager::initSpiBuses() {
#if defined(SPI_DESC_DATA)
  COMMON_DATA static BUS::SpiDesc g_descs[] = { SPI_DESC_DATA };
  static constexpr size_t count = sizeof(g_descs) / sizeof(g_descs[0]);
  COMMON_ZERO_INIT static uint8_t g_spiMem[count * sizeof(BUS::SpiBus)];
  for (size_t i = 0; i < count; i++)
    new (g_spiMem + i * sizeof(BUS::SpiBus)) BUS::SpiBus(g_descs[i]);
  _spiBuses = reinterpret_cast<BUS::SpiBus *>(g_spiMem);
  _spiCount = count;
#else
  _spiBuses = nullptr;
  _spiCount = 0;
#endif
}

void PeripheralsManager::initUartBuses() {
#if defined(UART_DESC_DATA)
  COMMON_DATA static BUS::UartBus g_buses[] = { BUS::UartBus{ UART_DESC_DATA } };
  _uartBuses = g_buses;
  _uartCount = sizeof(g_buses) / sizeof(g_buses[0]);
#else
  _uartBuses = nullptr;
  _uartCount = 0;
#endif
}

BUS::SpiBus &PeripheralsManager::spiBus(int idx) {
  COMMON_DATA static BUS::SpiBus s_dummy(
      BUS::SpiInstance::SpiNone,
      {GPIO::Port::PortNone, GPIO::Pin::PinNone},
      {GPIO::Port::PortNone, GPIO::Pin::PinNone},
      {GPIO::Port::PortNone, GPIO::Pin::PinNone},
      {GPIO::Port::PortNone, GPIO::Pin::PinNone});
  if (idx < 0 || idx >= static_cast<int>(_spiCount) || !_spiBuses)
    return s_dummy;
  return _spiBuses[idx];
}

BUS::UartBus &PeripheralsManager::uartBus(int idx) {
  COMMON_DATA static BUS::UartBus s_dummy(
      BUS::UartInstance::UartNone,
      {GPIO::Port::PortNone, GPIO::Pin::PinNone},
      {GPIO::Port::PortNone, GPIO::Pin::PinNone},
      115200);
  if (idx < 0 || idx >= static_cast<int>(_uartCount) || !_uartBuses)
    return s_dummy;
  return _uartBuses[idx];
}
