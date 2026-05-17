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

#pragma once

#include "BoardConfig.h"
#include "drivers/peripherals/bus/bus_spi.h"
#include "drivers/peripherals/bus/bus_uart.h"
#include "drivers/peripherals/timer.h"

namespace ThetaGP::Drivers::Peripheral {

class PeripheralsManager {
public:
  PeripheralsManager();

  static PeripheralsManager &getInstance() {
    static PeripheralsManager instance;
    return instance;
  }

  void initPeripherals();

  // ── Bus access ──
  BUS::SpiBus &spiBus(int idx);
  BUS::UartBus &uartBus(int idx);

  // ── Timer ──
  TIMER::Instance reservedTimer();

private:
  // ── Bus instance storage (raw storage + placement new) ──
#if defined(USE_SPI_COUNT) && USE_SPI_COUNT > 0
  alignas(BUS::SpiBus) uint8_t _spiBufStorage[
      sizeof(BUS::SpiBus) * USE_SPI_COUNT];
  BUS::SpiBus *_spiBuses = reinterpret_cast<BUS::SpiBus *>(_spiBufStorage);
#endif

#if defined(USE_UART_COUNT) && USE_UART_COUNT > 0
  alignas(BUS::UartBus) uint8_t _uartBufStorage[
      sizeof(BUS::UartBus) * USE_UART_COUNT];
  BUS::UartBus *_uartBuses = reinterpret_cast<BUS::UartBus *>(_uartBufStorage);
#endif

  void initSpiBuses();    // 从 SPI_DESC_DATA 构造
  void initUartBuses();   // 从 UART_DESC_DATA 构造
};

} // namespace ThetaGP::Drivers::Peripheral
