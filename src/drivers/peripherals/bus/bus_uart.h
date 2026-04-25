#pragma once

#include "utils/utils.h"

#include "drivers/peripherals/bus/bus.h"
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

struct UartDesc {
  UartInstance uartx;

  GPIO::PinDesc tx;
  GPIO::PinDesc rx;
  uint32_t baudrate;
};

class UartBus : public Bus {
private:
  UartDesc _desc;
  UART_HandleTypeDef _handle;
  void configTxRxPins();
  uint8_t getGpioAlternate();

public:
  UartBus() {}
  UartBus(UartInstance uartx, GPIO::PinDesc tx, GPIO::PinDesc rx,
          uint32_t baud);
  explicit UartBus(const UartDesc &desc);
  ~UartBus() = default;

  void enableClock() override;
  void init() override;

  RetVal write(uint8_t byte) override;
  RetVal write(uint8_t *bytes, uint16_t num) override;

  RetVal read(uint8_t *byte) override;
  RetVal read(uint8_t *bytes, uint16_t num) override;
};

class DebugUartBus : public UartBus {
public:
#if defined(DEBUG_UART)
  DebugUartBus(UartInstance uartx, GPIO::PinDesc tx, GPIO::PinDesc rx,
               uint32_t baud)
      : UartBus(uartx, tx, rx, baud) {}

  static DebugUartBus &getInstance() {
    using Port = GPIO::Port;
    using Pin = GPIO::Pin;
    static DebugUartBus instance(UartInstance::CONTACT(DEBUG_UART, _PERIPHERAL),
                                 CONTACT(DEBUG_UART, _TX_PIN),
                                 CONTACT(DEBUG_UART, _RX_PIN),
                                 CONTACT(DEBUG_UART, _BAUDRATE));
    return instance;
  }

  using UartBus::enableClock;
  using UartBus::init;

  RetVal write(uint8_t byte) override { UartBus::write(byte); };
  RetVal write(uint8_t *bytes, uint16_t num) override {
    UartBus::write(bytes, num);
  };

  RetVal read(uint8_t *byte) override { UartBus::read(byte); };
  RetVal read(uint8_t *bytes, uint16_t num) override {
    UartBus::read(bytes, num);
  };

#else
  static DebugUartBus &getInstance() {
    static DebugUartBus instance;
    return instance;
  }

  void init() { _initialized = false; };
#endif
};

} // namespace BUS
} // namespace Peripheral
} // namespace Drivers
} // namespace ThetaGP
