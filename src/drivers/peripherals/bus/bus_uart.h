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
#include "drivers/peripherals/gpio.h"

#include <cstdint>
#include <functional>

namespace ThetaGP {
namespace Drivers {
namespace Peripheral {
namespace BUS {

enum class Instance {
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

struct UartDesc {
  Instance uartx;

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

  using UartCallbackFunc = std::function<void(void *context)>;
  UartCallbackFunc _rxCallback;
  void *_rxContext = nullptr;
  UartCallbackFunc _txCallback;
  void *_txContext = nullptr;

  RetVal writeBytePolling(uint8_t byte) override;
  RetVal writeBytesPolling(uint8_t *bytes, uint16_t num) override;
  RetVal readBytePolling(uint8_t *byte) override;
  RetVal readBytesPolling(uint8_t *bytes, uint16_t num) override;

  RetVal writeByteInterrupt(uint8_t byte) override;
  RetVal writeBytesInterrupt(uint8_t *bytes, uint16_t num) override;
  RetVal readByteInterrupt(uint8_t *byte) override;
  RetVal readBytesInterrupt(uint8_t *bytes, uint16_t num) override;

public:
  UartBus(Instance uartx, GPIO::PinDesc tx, GPIO::PinDesc rx,
          uint32_t baudrate = 115200);
  explicit UartBus(const UartDesc &desc);
  ~UartBus() = default;

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
  void *halHandle() const { return _halHandle; }
};

} // namespace BUS
} // namespace Peripheral
} // namespace Drivers
} // namespace ThetaGP
