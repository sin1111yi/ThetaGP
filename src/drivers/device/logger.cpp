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

#include "drivers/device/logger.h"
#include "drivers/peripherals/bus/bus_uart.h"
#include "drivers/peripherals/systick.h"
#include "utils/log/log.h"

using namespace ThetaGP::Drivers::Device;
using ThetaGP::Drivers::Peripheral::BUS::Mode;
using ThetaGP::Drivers::Peripheral::BUS::UartBus;

// ── UART buffers ──
//   Borrowed by the bus, held for the firmware lifetime:
//   2 x 256 B = 512 B; UartBus::MAX_BUF_SIZE is the bus capacity constant.
COMMON_ZERO_INIT static uint8_t s_logTxBuf[UartBus::MAX_BUF_SIZE]{};
COMMON_ZERO_INIT static uint8_t s_logRxBuf[UartBus::MAX_BUF_SIZE]{};

static_assert(sizeof(s_logTxBuf) == UartBus::MAX_BUF_SIZE,
              "buffer size drifted");

Logger::Logger()
    : Device("logger"),
      _uart(Drivers::Peripheral::PeripheralsManager::getInstance().uartBus(
          LOGGER_UART)) {}

void Logger::init() {
  _uart.setBuffers(s_logTxBuf, s_logRxBuf, sizeof(s_logTxBuf));
  _uart.setMode(Mode::Polling);
  _uart.init();
  _initialized = true;

  LOG_INIT(LoggerTransmitBytes, delay_us);
  LOG_DEBUG("Logger Enabled!");
}

void Logger::LoggerTransmitBytes(uint8_t *data, uint16_t n) {
  auto &logger = getInstance();

  if (!logger._initialized)
    return;
  if (logger._uart.isTxBusy())
    return;
  (void)logger._uart.transmit(data, n);
}
