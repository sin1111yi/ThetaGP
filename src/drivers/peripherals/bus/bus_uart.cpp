#include "utils/types.h"

#include "drivers/peripherals/bus/bus_uart.h"
#include "drivers/peripherals/gpio.h"

#include <array>
#include <cstring>

#if defined(STM32H7)
#define UART_IRQ_GROUPS 8
#endif

using namespace ThetaGP::Drivers::Peripheral::BUS;
using namespace ThetaGP::Drivers::Peripheral::GPIO;
using ThetaGP::RetVal;

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

void enableBusUartClock(UartInstance uartx) {
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

UartBus::UartBus(UartInstance uartx, PinDesc tx, PinDesc rx, uint32_t baud) {
  setType(Type::Uart);
  allocBuf();

  _desc.uartx = uartx;
  _desc.tx = tx;
  _desc.rx = rx;
  _desc.baudrate = baud;
  _initialized = false;

  std::memset(&_handle, 0, sizeof(UART_HandleTypeDef));
}

UartBus::UartBus(const UartDesc &desc) {
  setType(Type::Uart);
  allocBuf();

  _desc = desc;
  _initialized = false;
  std::memset(&_handle, 0, sizeof(UART_HandleTypeDef));
}

void UartBus::enableClock() {
  RCC_PeriphCLKInitTypeDef periphClkInitStruct;

  std::memset(&periphClkInitStruct, 0, sizeof(RCC_PeriphCLKInitTypeDef));

  switch (_desc.uartx) {
  case UartInstance::Uart1:
  case UartInstance::Uart6:
    periphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USART16;
    periphClkInitStruct.Usart16ClockSelection = RCC_USART16CLKSOURCE_D2PCLK2;
    break;
  default:
    periphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USART234578;
    periphClkInitStruct.Usart16ClockSelection = RCC_USART16CLKSOURCE_D2PCLK2;
    break;
  }

  HAL_RCCEx_PeriphCLKConfig(&periphClkInitStruct);

  enableBusUartClock(_desc.uartx);
}

void UartBus::configTxRxPins() {
#if defined(STM32H7)
  uint32_t alternate = 0;

  // TODO: fix alternate
  switch (_desc.uartx) {
  case UartInstance::Uart4:
  case UartInstance::Uart5:
  case UartInstance::Uart8:
    alternate = 8;
    break;
  default:
    alternate = 4;
    break;
  }

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

  enableClock();
  configTxRxPins();

#if defined(STM32H7)
  const auto uartIdx = static_cast<uint32_t>(_desc.uartx);
  _handle.Instance = uartInstance[uartIdx];
  _handle.Init.BaudRate = _desc.baudrate;
  _handle.Init.WordLength = UART_WORDLENGTH_8B;
  _handle.Init.StopBits = UART_STOPBITS_1;
  _handle.Init.Parity = UART_PARITY_NONE;
  _handle.Init.Mode = UART_MODE_TX_RX;
  _handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  _handle.Init.OverSampling = UART_OVERSAMPLING_16;
  _handle.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  _handle.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  _handle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  HAL_UART_Init(&_handle);

  HAL_UARTEx_SetTxFifoThreshold(&_handle, UART_TXFIFO_THRESHOLD_1_8);
  HAL_UARTEx_SetRxFifoThreshold(&_handle, UART_RXFIFO_THRESHOLD_1_8);
  HAL_UARTEx_DisableFifoMode(&_handle);

#endif

  _initialized = true;
}

RetVal UartBus::write(uint8_t byte) {
#if defined(STM32H7)
  if (_initialized)
    HAL_UART_Transmit(&_handle, &byte, 1, 0x1000);
  else
    ;
#endif
  return RetVal::Ok;
}

RetVal UartBus::write(uint8_t *bytes, uint16_t num) {
#if defined(STM32H7)
  if (_initialized)
    HAL_UART_Transmit(&_handle, bytes, num, 0x1000);
  else
    ;
#endif
  return RetVal::Ok;
}

RetVal UartBus::read(uint8_t *byte) {
  (void)byte;
  return RetVal::Ok;
}

RetVal UartBus::read(uint8_t *bytes, uint16_t num) {
  (void)bytes;
  (void)num;
  return RetVal::Ok;
}