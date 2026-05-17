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
#include "drivers/device/devmem.h"
#include "utils/mempool/mempoolmanager.h"
#include "utils/log/log.h"

using namespace ThetaGP::Drivers::Device;
using ThetaGP::Drivers::Peripheral::BUS::Mode;

static constexpr uint16_t LOG_BUF_SIZE = 256;

Logger::Logger()
    : Device("logger"),
      _uart(Drivers::Peripheral::PeripheralsManager::getInstance().uartBus(
          LOGGER_UART)) {}

void Logger::init() {
  _txBuf = static_cast<uint8_t *>(
      Mempool::MempoolManager::alloc(
          Drivers::Device::DevMem::getInstance().poolId(), LOG_BUF_SIZE));
  _rxBuf = static_cast<uint8_t *>(
      Mempool::MempoolManager::alloc(
          Drivers::Device::DevMem::getInstance().poolId(), LOG_BUF_SIZE));

  _uart.setBuffers(_txBuf, _rxBuf, LOG_BUF_SIZE);
  _uart.setMode(Mode::Synchronous);
  _uart.init();
  _initialized = true;

  LOG_INIT(LoggerTransmitBytes);
  LOG_DEBUG("Logger Enabled!");
}

void Logger::LoggerTransmitBytes(uint8_t *data, uint16_t n) {
  auto &logger = getInstance();

  if (!logger._initialized)
    return;
  if (logger._uart.isTxBusy())
    return;
  logger._uart.write(data, n);
}
