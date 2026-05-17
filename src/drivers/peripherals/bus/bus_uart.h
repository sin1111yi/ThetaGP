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

#include "utils/utils.h"

#include "drivers/peripherals/bus/bus.h"
#include "drivers/peripherals/dma.h"
#include "drivers/peripherals/gpio.h"

#include <cstdint>

namespace ThetaGP {
namespace Drivers {
namespace Peripheral {
namespace BUS {

enum class UartInstance {
#if defined(STM32H7)
  Uart1,
  Uart2,
  Uart3,
  Uart4,
  Uart5,
  Uart6,
  Uart7,
  Uart8,
#endif
  UartNone = 0xFF,
};

// Abstract bus index → PeripheralsManager array index
#define BUS_UART_1 0
#define BUS_UART_2 1
#define BUS_UART_3 2
#define BUS_UART_4 3
#define BUS_UART_5 4
#define BUS_UART_6 5
#define BUS_UART_7 6
#define BUS_UART_8 7

struct UartDesc {
  UartInstance uartx;

  GPIO::PinDesc tx;
  GPIO::PinDesc rx;
  uint32_t baudrate;
};

class UartBus : public Bus {
private:
  static constexpr uint32_t _bufSize = 256;
  UartDesc _desc;
  void *_halHandle = nullptr;
  void configPins();

  // ── C-style ISR callback ──
  typedef void (*UartCallbackFunc)(void *context);
  UartCallbackFunc _rxCallback;
  void *_rxContext = nullptr;
  UartCallbackFunc _txCallback;
  void *_txContext = nullptr;

  // ── Subclass hooks (refactored base Bus interface) ──
  RetVal writeSync(const uint8_t *data, uint16_t len) override;
  RetVal readSync(uint8_t *data, uint16_t len) override;
  RetVal writeAsync(const uint8_t *data, uint16_t len) override;
  RetVal readAsync(uint8_t *data, uint16_t len) override;

public:
  UartBus(UartInstance uartx, GPIO::PinDesc tx, GPIO::PinDesc rx,
          uint32_t baudrate = 115200);
  explicit UartBus(const UartDesc &desc);
  ~UartBus() override;

  UartBus(const UartBus &) = delete;
  UartBus &operator=(const UartBus &) = delete;

  void init() override;
  void enableClock() override;

  void setRxCallback(UartCallbackFunc cb, void *context = nullptr);
  void setTxCallback(UartCallbackFunc cb, void *context = nullptr);
  void rxCallback() {
    if (_rxCallback) {
      _rxCallback(_rxContext);
    }
  }
  void txCallback() {
    if (_txCallback) {
      _txCallback(_txContext);
    }
  }

  bool isBusy() const;
  bool isTxBusy() const;
  bool isRxBusy() const;
  void *halHandle() const { return _halHandle; }

  // DMA read buffer (accessed by static completion callback)
  uint8_t *_readDmaBufPtr = nullptr;
  uint16_t _readDmaBufLen = 0;

  // DMA-safe RX buffer access (inherited protected member)
  uint8_t *rxBuf() const { return _rxBuf; }

  // DMA channels and state (accessed by static ISR callbacks in .cpp)
  DMA::DmaChannel *_dmaTx = nullptr;
  DMA::DmaChannel *_dmaRx = nullptr;
  volatile bool _idleDetectionEnabled = false;
};

} // namespace BUS
} // namespace Peripheral
} // namespace Drivers
} // namespace ThetaGP
