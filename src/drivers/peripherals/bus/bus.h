/**
 * @file bus.h
 * @brief Abstract base class for all BUS peripherals (SPI, UART, I2C)
 *
 * This is an abstract interface class - cannot be instantiated directly.
 * Subclasses must implement all pure virtual functions.
 */

#pragma once

#include "build_info.h"

#include "utils/types.h"
#include "utils/mempool/mempoolmanager.h"

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

  /**
   * @note For MCUs like STM32H7, DTCMRAM cannot be accessed by DMA.
   *       So I use two buffer pointers which will point to two memory regions
   *       after initialzed, these memory regions will be managed by busmem.
   */
  uint8_t *_pTxBuf;
  uint8_t *_pRxBuf;
  BusMem& _busMem;

  static constexpr size_t DEFAULT_TX_SIZE = 512;
  static constexpr size_t DEFAULT_RX_SIZE = 512;

  void allocBuf(size_t txSize = DEFAULT_TX_SIZE,
                size_t rxSize = DEFAULT_RX_SIZE);

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
};

} // namespace BUS
} // namespace Peripheral
} // namespace Drivers
} // namespace ThetaGP
