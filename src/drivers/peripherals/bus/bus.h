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

/**
 * @file bus.h
 * @brief Abstract base class for all BUS peripherals (SPI, UART, I2C)
 *
 * This is an abstract interface class - cannot be instantiated directly.
 * Subclasses must implement all pure virtual functions.
 */

#pragma once

#include "build_info.h"

#include "utils/mempool/mempoolmanager.h"
#include "utils/types.h"

#include "drivers/peripherals/bus/busmem.h"

namespace ThetaGP {
namespace Drivers {
namespace Peripheral {
namespace BUS {

enum class Type { Uart, Spi, I2c };
enum class Mode { Polling, Interrupt, DirectMemAccess };

/**
 * @brief Abstract base class for BUS peripherals
 *
 * This class defines the interface for all BUS types (SPI, UART, I2C).
 */
class Bus {
protected:
  Type _type;
  Mode _mode = Mode::Polling;
  bool _initialized = false;

  /**
   * @note For MCUs like STM32H7, DTCMRAM cannot be accessed by DMA.
   *       So I use two buffer pointers which will point to two memory regions
   *       after initialzed, these memory regions will be managed by busmem.
   */
  uint8_t *_pTxBuf = nullptr;
  uint8_t *_pRxBuf = nullptr;
  BusMem &_busMem;
  uint32_t _pTxBufSize;
  uint32_t _pRxBufSize;

  void allocBuf(uint32_t txSize, uint32_t rxSize);

  void freeBuf();

  Bus();

public:
  virtual ~Bus();
  void setType(Type type) { _type = type; }
  void setMode(Mode mode) { _mode = mode; }
  Type type() const { return _type; }
  Mode mode() const { return _mode; }

  virtual void init() = 0;
  virtual void enableClock() = 0;

  virtual RetVal write(uint8_t byte) = 0;
  virtual RetVal write(uint8_t *bytes, uint16_t num) = 0;

  virtual RetVal read(uint8_t *byte) = 0;
  virtual RetVal read(uint8_t *bytes, uint16_t num) = 0;

  bool isInitialized() { return _initialized; }
};

} // namespace BUS
} // namespace Peripheral
} // namespace Drivers
} // namespace ThetaGP
