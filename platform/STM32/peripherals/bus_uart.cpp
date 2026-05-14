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
#include "drivers/peripherals/dma.h"
#include "drivers/peripherals/gpio.h"
#include "drivers/peripherals/nvic.h"
#include "drivers/peripherals/nvic_exti.h"

#include "utils/log/log.h"
#include "drivers/peripherals/systick.h"

#include <array>
#include <cstring>

#if defined(STM32H7)
#define UART_IRQ_GROUPS 8
#endif

// Polling timeout (milliseconds)
static constexpr uint32_t UART_POLL_TIMEOUT_MS = 10;

using namespace ThetaGP::Drivers::Peripheral::BUS;
using namespace ThetaGP::Drivers::Peripheral::GPIO;
using ThetaGP::RetVal;

struct HalUart {
  UART_HandleTypeDef handle;
};

#define UART_HANDLE (static_cast<HalUart *>(_halHandle)->handle)

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

constexpr std::array<USART_TypeDef *, UART_IRQ_GROUPS> uartInstance = {
    USART1, USART2, USART3, UART4, UART5, USART6, UART7, UART8,
};

constexpr std::array<IRQn_Type, UART_IRQ_GROUPS> uartGroupIRQn = {
    USART1_IRQn, USART2_IRQn, USART3_IRQn, UART4_IRQn,
    UART5_IRQn,  USART6_IRQn, UART7_IRQn,  UART8_IRQn,
};
#else
#error "Unknown CPU"
#endif

// ── Safe enum-to-array-index mapping ──
static constexpr uint32_t uartInstanceIndex(Instance uart) noexcept {
  switch (uart) {
  case Instance::Uart1: return 0;
  case Instance::Uart2: return 1;
  case Instance::Uart3: return 2;
  case Instance::Uart4: return 3;
  case Instance::Uart5: return 4;
  case Instance::Uart6: return 5;
  case Instance::Uart7: return 6;
  case Instance::Uart8: return 7;
  default:              return UINT32_MAX;
  }
}

#if defined(STM32H7)
void enableBusUartClock(Instance uartx) {
  using ClockFunc = void (*)();
  static const std::array<ClockFunc, UART_IRQ_GROUPS> clockEnableTable = {{
      []() { __HAL_RCC_USART1_CLK_ENABLE(); },
      []() { __HAL_RCC_USART2_CLK_ENABLE(); },
      []() { __HAL_RCC_USART3_CLK_ENABLE(); },
      []() { __HAL_RCC_UART4_CLK_ENABLE(); },
      []() { __HAL_RCC_UART5_CLK_ENABLE(); },
      []() { __HAL_RCC_USART6_CLK_ENABLE(); },
      []() { __HAL_RCC_UART7_CLK_ENABLE(); },
      []() { __HAL_RCC_UART8_CLK_ENABLE(); },
  }};

  const auto index = uartInstanceIndex(uartx);
  if (index < clockEnableTable.size()) {
    clockEnableTable[index]();
  }
}
#endif

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

UartBus::~UartBus() {
  if (_dmaTx) {
    _dmaTx->deinit();
    delete _dmaTx;
    _dmaTx = nullptr;
  }
  if (_dmaRx) {
    _dmaRx->deinit();
    delete _dmaRx;
    _dmaRx = nullptr;
  }
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

  if (HAL_RCCEx_PeriphCLKConfig(&periphClkInitStruct) != HAL_OK) {
    LOG_ERROR("UART%u clock config failed",
              static_cast<uint32_t>(_desc.uartx) + 1);
  }

  enableBusUartClock(_desc.uartx);
}

void UartBus::configPins() {
#if defined(STM32H7)
  uint32_t txAlternate = lookupUartAf(_desc.uartx, _desc.tx.port, _desc.tx.pin);
  uint32_t rxAlternate = lookupUartAf(_desc.uartx, _desc.rx.port, _desc.rx.pin);

  Gpio tx(_desc.tx);
  Gpio rx(_desc.rx);

  tx.config(GPIO::Mode::AlternateFunctionPushPull, GPIO::Pull::NoPull,
            GPIO::Speed::Medium, txAlternate);
  rx.config(GPIO::Mode::AlternateFunctionPushPull, GPIO::Pull::NoPull,
            GPIO::Speed::Medium, rxAlternate);

  tx.init();
  rx.init();
#endif
}

// ── DMA completion callbacks (forward decl for init()) ──
static void uartTxDmaComplete(void *context);
static void uartRxDmaComplete(void *context);

void UartBus::init() {
  _pTxBufSize = _bufSize;
  _pRxBufSize = _bufSize;
  allocBuf(_bufSize, _bufSize);
  std::memset(_pRxBuf, 0, _pTxBufSize * sizeof(uint8_t));
  std::memset(_pTxBuf, 0, _pRxBufSize * sizeof(uint8_t));

  enableClock();
  configPins();

#if defined(STM32H7)
  const auto uartIdx = uartInstanceIndex(_desc.uartx);
  UART_HANDLE.Instance = uartInstance[uartIdx];
  UART_HANDLE.Init.BaudRate = _desc.baudrate;
  UART_HANDLE.Init.WordLength = UART_WORDLENGTH_8B;
  UART_HANDLE.Init.StopBits = UART_STOPBITS_1;
  UART_HANDLE.Init.Parity = UART_PARITY_NONE;
  UART_HANDLE.Init.Mode = UART_MODE_TX_RX;
  UART_HANDLE.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  UART_HANDLE.Init.OverSampling = UART_OVERSAMPLING_16;
  UART_HANDLE.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  UART_HANDLE.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  UART_HANDLE.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&UART_HANDLE) != HAL_OK) {
    LOG_ERROR("UART%u init failed", uartIdx + 1);
    return;
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&UART_HANDLE, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK) {
    LOG_ERROR("UART%u TX FIFO config failed", uartIdx + 1);
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&UART_HANDLE, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK) {
    LOG_ERROR("UART%u RX FIFO config failed", uartIdx + 1);
  }
  if (HAL_UARTEx_DisableFifoMode(&UART_HANDLE) != HAL_OK) {
    LOG_ERROR("UART%u FIFO disable failed", uartIdx + 1);
  }

  if (uartIdx < uartBusInstance.size()) {
    if (uartBusInstance[uartIdx] != nullptr) {
      LOG_WARN("UART%u already initialized, overwriting", uartIdx + 1);
    }
    uartBusInstance[uartIdx] = this;
  }
  const auto uartPrio =
      static_cast<uint32_t>(NVIC_EXTI::NvicPriority::PriorityLow);
  HAL_NVIC_SetPriority(uartGroupIRQn[uartIdx], NVIC_PRIORITY_BASE(uartPrio),
                       NVIC_PRIORITY_SUB(uartPrio));
  HAL_NVIC_EnableIRQ(uartGroupIRQn[uartIdx]);

  // ── DMA mode setup ──
  if (_mode == Mode::DirectMemAccess) {
    uint32_t txRequestId = 0;
    uint32_t rxRequestId = 0;
    switch (_desc.uartx) {
    case Instance::Uart1: txRequestId = DMA_REQUEST_USART1_TX; rxRequestId = DMA_REQUEST_USART1_RX; break;
    case Instance::Uart2: txRequestId = DMA_REQUEST_USART2_TX; rxRequestId = DMA_REQUEST_USART2_RX; break;
    case Instance::Uart3: txRequestId = DMA_REQUEST_USART3_TX; rxRequestId = DMA_REQUEST_USART3_RX; break;
    case Instance::Uart4: txRequestId = DMA_REQUEST_UART4_TX;  rxRequestId = DMA_REQUEST_UART4_RX;  break;
    case Instance::Uart5: txRequestId = DMA_REQUEST_UART5_TX;  rxRequestId = DMA_REQUEST_UART5_RX;  break;
    case Instance::Uart6: txRequestId = DMA_REQUEST_USART6_TX; rxRequestId = DMA_REQUEST_USART6_RX; break;
    case Instance::Uart7: txRequestId = DMA_REQUEST_UART7_TX;  rxRequestId = DMA_REQUEST_UART7_RX;  break;
    case Instance::Uart8: txRequestId = DMA_REQUEST_UART8_TX;  rxRequestId = DMA_REQUEST_UART8_RX;  break;
    default: break;
    }

    // TX: DMA1 Stream1 — memory to peripheral (buffer → UART TDR)
    _dmaTx = new DMA::DmaChannel(DMA::Controller::Dma1,
                                 DMA::Stream::Stream1);
    _dmaTx->setRequestId(txRequestId);
    _dmaTx->configure({
        .direction = DMA::Direction::MemoryToPeripheral,
        .srcDataWidth = DMA::DataWidth::Byte,
        .destDataWidth = DMA::DataWidth::Byte,
        .priority = DMA::Priority::Medium,
        .srcIncrement = true,
        .destIncrement = false,
    });
    _dmaTx->setCallback(uartTxDmaComplete, this);
    _dmaTx->init();

    // RX: DMA1 Stream0 — peripheral to memory (UART RDR → buffer)
    _dmaRx = new DMA::DmaChannel(DMA::Controller::Dma1,
                                 DMA::Stream::Stream0);
    _dmaRx->setRequestId(rxRequestId);
    _dmaRx->configure({
        .direction = DMA::Direction::PeripheralToMemory,
        .srcDataWidth = DMA::DataWidth::Byte,
        .destDataWidth = DMA::DataWidth::Byte,
        .priority = DMA::Priority::Medium,
        .srcIncrement = false,
        .destIncrement = true,
    });
    _dmaRx->setCallback(uartRxDmaComplete, this);
    _dmaRx->init();
  }
#endif

  Bus::init();
}

RetVal UartBus::writeBytePolling(uint8_t byte) {
#if defined(STM32H7)
  if (_initialized) {
    if (_pTxBuf != NULL && 1 <= _pTxBufSize) {
      _pTxBuf[0] = byte;
      if (HAL_UART_Transmit(&UART_HANDLE, _pTxBuf, 1, UART_POLL_TIMEOUT_MS) != HAL_OK) {
        return RetVal::Error;
      }
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
      if (HAL_UART_Transmit(&UART_HANDLE, _pTxBuf, num, UART_POLL_TIMEOUT_MS) != HAL_OK) {
        return RetVal::Error;
      }
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
      if (HAL_OK == HAL_UART_Receive(&UART_HANDLE, _pRxBuf, 1, UART_POLL_TIMEOUT_MS)) {
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
      if (HAL_OK == HAL_UART_Receive(&UART_HANDLE, _pRxBuf, num, UART_POLL_TIMEOUT_MS)) {
        std::memcpy(bytes, _pRxBuf, num);
        return RetVal::Ok;
      }
    }
  }
#endif
  return RetVal::Error;
}

void UartBus::setRxCallback(UartCallbackFunc cb, void *context) {
  _rxCallback = cb;
  _rxContext = context;
}

void UartBus::setTxCallback(UartCallbackFunc cb, void *context) {
  _txCallback = cb;
  _txContext = context;
}

RetVal UartBus::writeByteInterrupt(uint8_t byte) {
#if defined(STM32H7)
  if (_initialized) {
    if (isBusy()) {
      return RetVal::Busy;
    }
    _pTxBuf[0] = byte;
    return HAL_UART_Transmit_IT(&UART_HANDLE, _pTxBuf, 1) == HAL_OK
               ? RetVal::Ok
               : RetVal::Error;
  }
#endif
  return RetVal::Error;
}

RetVal UartBus::writeBytesInterrupt(uint8_t *bytes, uint16_t num) {
#if defined(STM32H7)
  if (_initialized && bytes && num > 0 && num <= _pTxBufSize) {
    if (isBusy()) {
      return RetVal::Busy;
    }
    std::memcpy(_pTxBuf, bytes, num);
    return HAL_UART_Transmit_IT(&UART_HANDLE, _pTxBuf, num) == HAL_OK
               ? RetVal::Ok
               : RetVal::Error;
  }
#endif
  return RetVal::Error;
}

RetVal UartBus::readByteInterrupt(uint8_t *byte) {
#if defined(STM32H7)
  if (_initialized && byte) {
    return HAL_UART_Receive_IT(&UART_HANDLE, byte, 1) == HAL_OK
               ? RetVal::Ok
               : RetVal::Error;
  }
#endif
  return RetVal::Error;
}

RetVal UartBus::readBytesInterrupt(uint8_t *bytes, uint16_t num) {
#if defined(STM32H7)
  if (_initialized && bytes && num > 0 && num <= _pRxBufSize) {
    return HAL_UART_Receive_IT(&UART_HANDLE, bytes, num) == HAL_OK
               ? RetVal::Ok
               : RetVal::Error;
  }
#endif
  return RetVal::Error;
}

// ── DMA mode ──
// Uses LL-level DMA control directly: DmaChannel::start() to set up the
// DMA stream, then manually enable UART CR3 bits to trigger DMA requests.
// Completion is dispatched through DmaChannel's callback.
// No HAL_UART_Transmit_DMA / HAL_DMA_Start_IT dependency.

static void uartTxDmaComplete(void *context) {
  auto *uart = static_cast<UartBus *>(context);
  if (!uart) return;

  auto *huart = &static_cast<HalUart *>(uart->halHandle())->handle;
  huart->Instance->CR3 &= ~USART_CR3_DMAT;
  huart->gState = HAL_UART_STATE_READY;

  uart->txCallback();
}

static void uartRxDmaComplete(void *context) {
  auto *uart = static_cast<UartBus *>(context);
  if (!uart) return;

  auto *huart = &static_cast<HalUart *>(uart->halHandle())->handle;
  huart->Instance->CR3 &= ~USART_CR3_DMAR;
  huart->Instance->CR1 &= ~USART_CR1_IDLEIE;
  uart->_idleDetectionEnabled = false;
  huart->RxState = HAL_UART_STATE_READY;

  // Copy from DMA-safe _pRxBuf to caller buffer (may be in DTCM)
  if (uart->_readDmaBufPtr && uart->_readDmaBufLen > 0) {
    std::memcpy(uart->_readDmaBufPtr, uart->rxBuf(), uart->_readDmaBufLen);
    uart->_readDmaBufPtr = nullptr;
    uart->_readDmaBufLen = 0;
    uart->rxCallback();
  }
}

// ── DMA TX ──
RetVal UartBus::writeByteDMA(uint8_t byte) {
#if defined(STM32H7)
  if (_initialized && _dmaTx && _pTxBuf != nullptr) {
    if (isBusy()) {
      return RetVal::Busy;
    }
    _pTxBuf[0] = byte;
    auto *huart = &static_cast<HalUart *>(_halHandle)->handle;
    UART_HANDLE.gState = HAL_UART_STATE_BUSY_TX;
    _dmaTx->start(reinterpret_cast<uint32_t>(_pTxBuf),
                  reinterpret_cast<uint32_t>(&huart->Instance->TDR), 1);
    huart->Instance->CR3 |= USART_CR3_DMAT;
    return RetVal::Ok;
  }
#endif
  return RetVal::Error;
}

RetVal UartBus::writeBytesDMA(uint8_t *bytes, uint16_t num) {
#if defined(STM32H7)
  if (_initialized && _dmaTx && bytes && num > 0 && num <= _pTxBufSize && _pTxBuf != nullptr) {
    if (isBusy()) {
      return RetVal::Busy;
    }
    std::memcpy(_pTxBuf, bytes, num);
    auto *huart = &static_cast<HalUart *>(_halHandle)->handle;
    UART_HANDLE.gState = HAL_UART_STATE_BUSY_TX;
    _dmaTx->start(reinterpret_cast<uint32_t>(_pTxBuf),
                  reinterpret_cast<uint32_t>(&huart->Instance->TDR), num);
    huart->Instance->CR3 |= USART_CR3_DMAT;
    return RetVal::Ok;
  }
#endif
  return RetVal::Error;
}

RetVal UartBus::readByteDMA(uint8_t *byte) {
#if defined(STM32H7)
  if (_initialized && _dmaRx && byte && _pRxBuf != nullptr) {
    if (isBusy()) {
      return RetVal::Busy;
    }
    // Store caller buffer pointer; DMA into _pRxBuf (DMA-safe),
    // then copy to caller buffer in completion callback
    _readDmaBufPtr = byte;
    _readDmaBufLen = 1;
    auto *huart = &static_cast<HalUart *>(_halHandle)->handle;
    UART_HANDLE.RxState = HAL_UART_STATE_BUSY_RX;
    _dmaRx->start(reinterpret_cast<uint32_t>(&huart->Instance->RDR),
                  reinterpret_cast<uint32_t>(_pRxBuf), 1);
    huart->Instance->CR3 |= USART_CR3_DMAR;
    return RetVal::Ok;
  }
#endif
  return RetVal::Error;
}

RetVal UartBus::readBytesDMAIdle(uint8_t *bytes, uint16_t num) {
#if defined(STM32H7)
  if (_initialized && _dmaRx && bytes && num > 0 && num <= _pRxBufSize && _pRxBuf != nullptr) {
    if (isBusy()) {
      return RetVal::Busy;
    }
    // Store caller buffer pointer; DMA into _pRxBuf (DMA-safe),
    // then copy to caller buffer in completion callback or idle ISR
    _readDmaBufPtr = bytes;
    _readDmaBufLen = num;
    _idleDetectionEnabled = true;
    auto *huart = &static_cast<HalUart *>(_halHandle)->handle;
    UART_HANDLE.RxState = HAL_UART_STATE_BUSY_RX;
    _dmaRx->start(reinterpret_cast<uint32_t>(&huart->Instance->RDR),
                  reinterpret_cast<uint32_t>(_pRxBuf), num);
    huart->Instance->CR3 |= USART_CR3_DMAR;
    // Enable UART idle line detection interrupt
    huart->Instance->CR1 |= USART_CR1_IDLEIE;
    return RetVal::Ok;
  }
#endif
  return RetVal::Error;
}

bool UartBus::isBusy() const {
#if defined(STM32H7)
  return UART_HANDLE.gState != HAL_UART_STATE_READY ||
         UART_HANDLE.RxState != HAL_UART_STATE_READY;
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
    LOG_WARN("UART error: PE=%d FE=%d NE=%d ORE=%d",
             !!(isr & USART_ISR_PE), !!(isr & USART_ISR_FE),
             !!(isr & USART_ISR_NE), !!(isr & USART_ISR_ORE));
  }

  // ── Data transfer (ISR context) ──
  // Calls huart->RxISR/TxISR (HAL internal function pointers initialized
  // by HAL_UART_Init). These handle register-level data movement, FIFO,
  // and buffer management. Direct register replication would be ~200 LoC.
  //
  // rxCallback/txCallback run in ISR context — keep them lightweight
  // (no blocking, no allocation, no mutex).

  // ── Idle line detection (DMA variable-length read) ──
  // When IDLE is set and a DMA read is active, stop the DMA
  // and determine the actual byte count from the remaining NDTR.
  if ((isr & USART_ISR_IDLE) && instance->_idleDetectionEnabled &&
      instance->_dmaRx && instance->_readDmaBufPtr != nullptr) {
    UARTx->ICR = USART_ICR_IDLECF;
    instance->_dmaRx->stop();
    UARTx->CR3 &= ~USART_CR3_DMAR;
    UARTx->CR1 &= ~USART_CR1_IDLEIE;
    instance->_idleDetectionEnabled = false;
    huart->RxState = HAL_UART_STATE_READY;

    uint16_t remaining = instance->_dmaRx->getRemainingCount();
    uint16_t received = (remaining < instance->_readDmaBufLen)
                            ? (instance->_readDmaBufLen - remaining)
                            : 0;
    if (received > 0 && instance->_readDmaBufPtr != nullptr) {
      std::memcpy(instance->_readDmaBufPtr, instance->rxBuf(), received);
    }
    instance->_readDmaBufLen = received;
    instance->_readDmaBufPtr = nullptr;
    instance->rxCallback();
    // Skip remaining ISR processing — the read is complete
    return;
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
    if (huart->Lock != HAL_LOCKED) {
      huart->gState = HAL_UART_STATE_READY;
    }
    instance->txCallback();
  }
}

#define uartIrqHandler(name, idx)                                              \
  void name(void) { UARTx_IRQHandler(idx); }                                   \
  struct dummy

#define UART(x) uartInstanceIndex(Instance::Uart##x)

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
