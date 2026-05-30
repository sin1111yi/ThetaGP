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
  [[nodiscard]] BUS::SpiBus &spiBus(int idx);
  [[nodiscard]] BUS::UartBus &uartBus(int idx);

  // ── Timer ──
  [[nodiscard]] TIMER::Instance reservedTimer();

private:
  BUS::SpiBus *_spiBuses = nullptr;
  uint8_t _spiCount = 0;
  BUS::UartBus *_uartBuses = nullptr;
  uint8_t _uartCount = 0;

  void initSpiBuses();
  void initUartBuses();
};

} // namespace ThetaGP::Drivers::Peripheral
