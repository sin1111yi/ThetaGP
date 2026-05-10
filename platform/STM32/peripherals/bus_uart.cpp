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

#include "utils/types.h"

#include "drivers/peripherals/bus/bus_uart.h"
#include "drivers/peripherals/gpio.h"
#include "drivers/peripherals/nvic.h"
#include "drivers/peripherals/nvic_exti.h"

#include <array>
#include <cstring>

#if defined(STM32H7)
#define UART_IRQ_GROUPS 8
#endif

using namespace ThetaGP::Drivers::Peripheral::BUS;
using namespace ThetaGP::Drivers::Peripheral::GPIO;
using ThetaGP::RetVal;

struct HalUart {
  UART_HandleTypeDef handle;
};

#define HANDLE (static_cast<HalUart *>(_halHandle)->handle)

static constexpr struct {
  Instance uart;
  Port port;
  Pin pin;
  uint8_t af;
} uartPinAfTable[] = {
    {Instance::Uart1, Port::PortA, Pin::Pin9, 7},
    {Instance::Uart1, Port::PortA, Pin::Pin10, 7},
    {Instance::Uart1, Port::PortB, Pin::Pin14, 4},
    {Instance::Uart1, Port::PortB, Pin::Pin15, 4},
    {Instance::Uart2, Port::PortA, Pin::Pin2, 7},
    {Instance::Uart2, Port::PortA, Pin::Pin3, 7},
    {Instance::Uart3, Port::PortB, Pin::Pin10, 7},
    {Instance::Uart3, Port::PortB, Pin::Pin11, 7},
    {Instance::Uart4, Port::PortA, Pin::Pin0, 8},
    {Instance::Uart4, Port::PortA, Pin::Pin1, 8},
    {Instance::Uart4, Port::PortB, Pin::Pin8, 8},
    {Instance::Uart4, Port::PortB, Pin::Pin9, 8},
    {Instance::Uart5, Port::PortB, Pin::Pin5, 8},
    {Instance::Uart5, Port::PortB, Pin::Pin6, 8},
    {Instance::Uart6, Port::PortC, Pin::Pin6, 7},
    {Instance::Uart6, Port::PortC, Pin::Pin7, 7},
    {Instance::Uart7, Port::PortF, Pin::Pin7, 7},
    {Instance::Uart7, Port::PortF, Pin::Pin8, 7},
    {Instance::Uart8, Port::PortJ, Pin::Pin8, 8},
    {Instance::Uart8, Port::PortJ, Pin::Pin9, 8},
};

static uint8_t lookupUartAf(Instance uart, Port port, Pin pin) {
  for (auto &entry : uartPinAfTable) {
    if (entry.uart == uart && entry.port == port && entry.pin == pin)
      return entry.af;
  }
  return 0;
}

#if defined(STM32H7)
static std::array<UartBus *, UART_IRQ_GROUPS> uartBusInstance = {};

const std::array<USART_TypeDef *, UART_IRQ_GROUPS> uartInstance = {
    USART1, USART2, USART3, UART4, UART5, USART6, UART7, UART8,
};

constexpr std::array<IRQn_Type, UART_IRQ_GROUPS> uartGroupIRQn = {
    USART1_IRQn, USART2_IRQn, USART3_IRQn, UART4_IRQn,
    UART5_IRQn,  USART6_IRQn, UART7_IRQn,  UART8_IRQn,
};
#else
#error "Unknown CPU"
#endif

void enableBusUartClock(Instance uartx) {
  using ClockFunc = void (*)();
  static const std::array<ClockFunc, UART_IRQ_GROUPS> clockEnableTable = {{
#if defined(STM32H7)
      []() { __HAL_RCC_USART1_CLK_ENABLE(); },
      []() { __HAL_RCC_USART2_CLK_ENABLE(); },
      []() { __HAL_RCC_USART3_CLK_ENABLE(); },
      []() { __HAL_RCC_UART4_CLK_ENABLE(); },
      []() { __HAL_RCC_UART5_CLK_ENABLE(); },
      []() { __HAL_RCC_USART6_CLK_ENABLE(); },
      []() { __HAL_RCC_UART7_CLK_ENABLE(); },
      []() { __HAL_RCC_UART8_CLK_ENABLE(); },
#endif
  }};

  const auto index = static_cast<size_t>(uartx);
  if (index < clockEnableTable.size()) {
    clockEnableTable[index]();
  }
}

UartBus::UartBus(Instance uartx, PinDesc tx, PinDesc rx, uint32_t baudrate) {
  _halHandle = new HalUart();
  setType(Type::Uart);

  _desc.uartx = uartx;
  _desc.tx = tx;
  _desc.rx = rx;
  _desc.baudrate = baudrate;

  std::memset(_halHandle, 0, sizeof(HalUart));
}

UartBus::UartBus(const UartDesc &desc) {
  _halHandle = new HalUart();
  setType(Type::Uart);

  _desc = desc;
  std::memset(_halHandle, 0, sizeof(HalUart));
}

void UartBus::enableClock() {
  RCC_PeriphCLKInitTypeDef periphClkInitStruct;
  std::memset(&periphClkInitStruct, 0, sizeof(RCC_PeriphCLKInitTypeDef));

  switch (_desc.uartx) {
  case Instance::Uart1:
  case Instance::Uart6:
    periphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USART16;
    periphClkInitStruct.Usart16ClockSelection = RCC_USART16CLKSOURCE_D2PCLK2;
    break;
  default:
    periphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USART234578;
    periphClkInitStruct.Usart234578ClockSelection =
        RCC_USART234578CLKSOURCE_D2PCLK1;
    break;
  }

  HAL_RCCEx_PeriphCLKConfig(&periphClkInitStruct);

  enableBusUartClock(_desc.uartx);
}

void UartBus::configPins() {
#if defined(STM32H7)
  uint32_t alternate = lookupUartAf(_desc.uartx, _desc.tx.port, _desc.tx.pin);

  Gpio tx(_desc.tx);
  Gpio rx(_desc.rx);

  tx.config(GPIO::Mode::AlternateFunctionPushPull, GPIO::Pull::NoPull,
            GPIO::Speed::Medium, alternate);
  rx.config(GPIO::Mode::AlternateFunctionPushPull, GPIO::Pull::NoPull,
            GPIO::Speed::Medium, alternate);

  tx.init();
  rx.init();
#endif
}

void UartBus::init() {
  _pTxBufSize = _bufSize;
  _pRxBufSize = _bufSize;
  allocBuf(_bufSize, _bufSize);
  std::memset(_pRxBuf, 0, _pTxBufSize * sizeof(uint8_t));
  std::memset(_pTxBuf, 0, _pRxBufSize * sizeof(uint8_t));

  enableClock();
  configPins();

#if defined(STM32H7)
  const auto uartIdx = static_cast<uint32_t>(_desc.uartx);
  HANDLE.Instance = uartInstance[uartIdx];
  HANDLE.Init.BaudRate = _desc.baudrate;
  HANDLE.Init.WordLength = UART_WORDLENGTH_8B;
  HANDLE.Init.StopBits = UART_STOPBITS_1;
  HANDLE.Init.Parity = UART_PARITY_NONE;
  HANDLE.Init.Mode = UART_MODE_TX_RX;
  HANDLE.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  HANDLE.Init.OverSampling = UART_OVERSAMPLING_16;
  HANDLE.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  HANDLE.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  HANDLE.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  HAL_UART_Init(&HANDLE);
  HAL_UARTEx_SetTxFifoThreshold(&HANDLE, UART_TXFIFO_THRESHOLD_1_8);
  HAL_UARTEx_SetRxFifoThreshold(&HANDLE, UART_RXFIFO_THRESHOLD_1_8);
  HAL_UARTEx_DisableFifoMode(&HANDLE);

  if (uartIdx < uartBusInstance.size()) {
    uartBusInstance[uartIdx] = this;
  }
  const auto uartPrio =
      static_cast<uint32_t>(NVIC_EXTI::NvicPriority::PriorityLow);
  HAL_NVIC_SetPriority(uartGroupIRQn[uartIdx], NVIC_PRIORITY_BASE(uartPrio),
                       NVIC_PRIORITY_SUB(uartPrio));
  HAL_NVIC_EnableIRQ(uartGroupIRQn[uartIdx]);
#endif

  Bus::init();
}

RetVal UartBus::writeBytePolling(uint8_t byte) {
#if defined(STM32H7)
  if (_initialized) {
    if (_pTxBuf != NULL && 1 <= _pTxBufSize) {
      _pTxBuf[0] = byte;
      HAL_UART_Transmit(&HANDLE, _pTxBuf, 1, 10);
      return RetVal::Ok;
    }
  }
#endif
  return RetVal::Error;
}

RetVal UartBus::writeBytesPolling(uint8_t *bytes, uint16_t num) {
#if defined(STM32H7)
  if (_initialized) {
    if (_pTxBuf != NULL && num <= _pTxBufSize) {
      std::memcpy(_pTxBuf, bytes, num * sizeof(uint8_t));
      HAL_UART_Transmit(&HANDLE, _pTxBuf, num, 10);
      return RetVal::Ok;
    }
  }
#endif
  return RetVal::Error;
}

RetVal UartBus::readBytePolling(uint8_t *byte) {
#if defined(STM32H7)
  if (_initialized) {
    if (_pRxBuf != NULL && 1 <= _pRxBufSize) {
      if (HAL_OK == HAL_UART_Receive(&HANDLE, _pRxBuf, 1, 10)) {
        *byte = _pRxBuf[0];
        return RetVal::Ok;
      }
    }
  }
#endif
  return RetVal::Error;
}

RetVal UartBus::readBytesPolling(uint8_t *bytes, uint16_t num) {
#if defined(STM32H7)
  if (_initialized) {
    if (_pRxBuf != NULL && num <= _pRxBufSize) {
      if (HAL_OK == HAL_UART_Receive(&HANDLE, _pRxBuf, num, 10)) {
        std::memcpy(bytes, _pRxBuf, num);
        return RetVal::Ok;
      }
    }
  }
#endif
  return RetVal::Error;
}

void UartBus::setRxCallback(UartCallbackFunc cb, void *context) {
  _rxCallback = std::move(cb);
  _rxContext = context;
}

void UartBus::setTxCallback(UartCallbackFunc cb, void *context) {
  _txCallback = std::move(cb);
  _txContext = context;
}

RetVal UartBus::writeByteInterrupt(uint8_t byte) {
#if defined(STM32H7)
  if (_initialized) {
    _pTxBuf[0] = byte;
    return HAL_UART_Transmit_IT(&HANDLE, _pTxBuf, 1) == HAL_OK
               ? RetVal::Ok
               : RetVal::Error;
  }
#endif
  return RetVal::Error;
}

RetVal UartBus::writeBytesInterrupt(uint8_t *bytes, uint16_t num) {
#if defined(STM32H7)
  if (_initialized && bytes && num > 0 && num <= _pTxBufSize) {
    std::memcpy(_pTxBuf, bytes, num);
    return HAL_UART_Transmit_IT(&HANDLE, _pTxBuf, num) == HAL_OK
               ? RetVal::Ok
               : RetVal::Error;
  }
#endif
  return RetVal::Error;
}

RetVal UartBus::readByteInterrupt(uint8_t *byte) {
#if defined(STM32H7)
  if (_initialized && byte) {
    return HAL_UART_Receive_IT(&HANDLE, byte, 1) == HAL_OK
               ? RetVal::Ok
               : RetVal::Error;
  }
#endif
  return RetVal::Error;
}

RetVal UartBus::readBytesInterrupt(uint8_t *bytes, uint16_t num) {
#if defined(STM32H7)
  if (_initialized && bytes && num > 0) {
    return HAL_UART_Receive_IT(&HANDLE, bytes, num) == HAL_OK
               ? RetVal::Ok
               : RetVal::Error;
  }
#endif
  return RetVal::Error;
}

bool UartBus::isBusy() const {
#if defined(STM32H7)
  return HANDLE.gState != HAL_UART_STATE_READY;
#else
  return false;
#endif
}

extern "C" {

#if defined(STM32H7)

static void UARTx_IRQHandler(uint32_t uartIdx) {
  auto *instance = uartBusInstance[uartIdx];
  if (!instance) {
    return;
  }

  auto *huart = &static_cast<HalUart *>(instance->halHandle())->handle;
  auto *UARTx = huart->Instance;

  uint32_t isr = UARTx->ISR;
  uint32_t cr1 = UARTx->CR1;

  if (isr & (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE)) {
    uint32_t icrMask = 0;
    if (isr & USART_ISR_PE) icrMask |= USART_ICR_PECF;
    if (isr & USART_ISR_FE) icrMask |= USART_ICR_FECF;
    if (isr & USART_ISR_NE) icrMask |= USART_ICR_NECF;
    if (isr & USART_ISR_ORE) icrMask |= USART_ICR_ORECF;
    UARTx->ICR = icrMask;
  }

  if ((isr & USART_ISR_RXNE_RXFNE) && (cr1 & USART_CR1_RXNEIE_RXFNEIE)) {
    huart->RxISR(huart);
    if (huart->RxState == HAL_UART_STATE_READY) {
      instance->rxCallback();
    }
  }

  if ((isr & USART_ISR_TXE_TXFNF) && (cr1 & USART_CR1_TXEIE_TXFNFIE)) {
    huart->TxISR(huart);
  }

  if ((isr & USART_ISR_TC) && (cr1 & USART_CR1_TCIE)) {
    UARTx->ICR = USART_ICR_TCCF;
    UARTx->CR1 &= ~USART_CR1_TCIE;
    huart->gState = HAL_UART_STATE_READY;
    instance->txCallback();
  }
}

#define uartIrqHandler(name, idx)                                              \
  void name(void) { UARTx_IRQHandler(idx); }                                   \
  struct dummy

#define UART(x) static_cast<uint32_t>(Instance::Uart##x)

uartIrqHandler(USART1_IRQHandler, UART(1));
uartIrqHandler(USART2_IRQHandler, UART(2));
uartIrqHandler(USART3_IRQHandler, UART(3));
uartIrqHandler(UART4_IRQHandler, UART(4));
uartIrqHandler(UART5_IRQHandler, UART(5));
uartIrqHandler(USART6_IRQHandler, UART(6));
uartIrqHandler(UART7_IRQHandler, UART(7));
uartIrqHandler(UART8_IRQHandler, UART(8));

#endif
}
