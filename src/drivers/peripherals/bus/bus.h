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
 * Uses pointer-to-member-function for dispatch. Subclasses override
 * the read/write functions they support; the base class sets the
 * function pointers during init() based on _mode.
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

  // --- pointer-to-member-function callback dispatch ---
  using WriteByteFn = RetVal (Bus::*)(uint8_t);
  using WriteBytesFn = RetVal (Bus::*)(uint8_t *, uint16_t);
  using ReadByteFn = RetVal (Bus::*)(uint8_t *);
  using ReadBytesFn = RetVal (Bus::*)(uint8_t *, uint16_t);

  WriteByteFn _writeByteFn = nullptr;
  WriteBytesFn _writeBytesFn = nullptr;
  ReadByteFn _readByteFn = nullptr;
  ReadBytesFn _readBytesFn = nullptr;

  void setupCallbacks();

  // --- mode-specific virtual functions (default: error) ---
  virtual RetVal writeBytePolling(uint8_t byte) {
    UNUSED(byte);
    return RetVal::Error;
  }
  virtual RetVal writeBytesPolling(uint8_t *bytes, uint16_t num) {
    UNUSED(bytes);
    UNUSED(num);
    return RetVal::Error;
  }
  virtual RetVal readBytePolling(uint8_t *byte) {
    UNUSED(byte);
    return RetVal::Error;
  }
  virtual RetVal readBytesPolling(uint8_t *bytes, uint16_t num) {
    UNUSED(bytes);
    UNUSED(num);
    return RetVal::Error;
  }

  virtual RetVal writeByteInterrupt(uint8_t byte) {
    UNUSED(byte);
    return RetVal::Error;
  }
  virtual RetVal writeBytesInterrupt(uint8_t *bytes, uint16_t num) {
    UNUSED(bytes);
    UNUSED(num);
    return RetVal::Error;
  }
  virtual RetVal readByteInterrupt(uint8_t *byte) {
    UNUSED(byte);
    return RetVal::Error;
  }
  virtual RetVal readBytesInterrupt(uint8_t *bytes, uint16_t num) {
    UNUSED(bytes);
    UNUSED(num);
    return RetVal::Error;
  }

  virtual RetVal writeByteDMA(uint8_t byte) {
    UNUSED(byte);
    return RetVal::Error;
  }
  virtual RetVal writeBytesDMA(uint8_t *bytes, uint16_t num) {
    UNUSED(bytes);
    UNUSED(num);
    return RetVal::Error;
  }
  virtual RetVal readByteDMA(uint8_t *byte) {
    UNUSED(byte);
    return RetVal::Error;
  }
  virtual RetVal readBytesDMA(uint8_t *bytes, uint16_t num) {
    UNUSED(bytes);
    UNUSED(num);
    return RetVal::Error;
  }

public:
  virtual ~Bus();
  void setType(Type type) { _type = type; }
  void setMode(Mode mode) { _mode = mode; }
  Type type() const { return _type; }
  Mode mode() const { return _mode; }

  virtual void init();
  virtual void enableClock() = 0;

  RetVal write(uint8_t byte);
  RetVal write(uint8_t *bytes, uint16_t num);

  RetVal read(uint8_t *byte);
  RetVal read(uint8_t *bytes, uint16_t num);

  bool isInitialized() { return _initialized; }
};

} // namespace BUS
} // namespace Peripheral
} // namespace Drivers
} // namespace ThetaGP
