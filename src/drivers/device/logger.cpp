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

#include "utils/log/log.h"

#include "drivers/device/logger.h"
#include "drivers/peripherals/bus/bus.h"
#include "drivers/peripherals/bus/bus_uart.h"
#include "utils/types.h"

#include <cstdint>
#include <cstring>

using namespace ThetaGP::Drivers::Peripheral::BUS;

namespace ThetaGP::Drivers::Device {

Logger::Logger() : Device("logger") {}

void Logger::init() {
#if defined(LOGGER_UART)
  _uart.setMode(Mode::Asynchronous);
  _uart.init();
#endif
  _initialized = true;

  LOG_INIT(LoggerTransmitBytes);
  LOG_DEBUG("Logger Enabled!");
}

void Logger::LoggerTransmitBytes(uint8_t *data, uint16_t n) {
  auto &self = getInstance();

  if (self.isInitialized()) {
#if defined(LOGGER_UART)
    if (self._uart.isTxBusy())
      return;
    self._uart.write(data, n);
#endif
  }
}

} // namespace ThetaGP::Drivers::Device
