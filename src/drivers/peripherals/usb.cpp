#include "drivers/peripherals/usb.h"
#include "drivers/peripherals/gpio.h"

#include <array>

namespace USB {

#if defined(STM32H7)

using P = GpioDefine::Port;
using N = GpioDefine::Pin;

// ULPI GPIO array, index corresponds to UlpiPin enum value
std::array<Gpio, 12> kUlpiGpios = {{
    Gpio({P::PortA, N::Pin5}),  // Clk
    Gpio({P::PortC, N::Pin0}),  // Stp
    Gpio({P::PortC, N::Pin2}),  // Dir
    Gpio({P::PortC, N::Pin3}),  // Nxt
    Gpio({P::PortA, N::Pin3}),  // D0
    Gpio({P::PortB, N::Pin0}),  // D1
    Gpio({P::PortB, N::Pin1}),  // D2
    Gpio({P::PortB, N::Pin10}), // D3
    Gpio({P::PortB, N::Pin11}), // D4
    Gpio({P::PortB, N::Pin12}), // D5
    Gpio({P::PortB, N::Pin13}), // D6
    Gpio({P::PortB, N::Pin5}),  // D7
}};

constexpr uint32_t kUlpiAlternate = GPIO_AF10_OTG2_HS;

#endif

USB::USB(USBSpeed speed, USBPeripheral peripheral)
    : _initialized(false), _speed(speed), _peripheral(peripheral) {}

void USB::init(void) {
  if (_speed == USBSpeed::UsbHighSpeedExternalPHY &&
      _peripheral == USBPeripheral::UsbULPI) {
#if defined(STM32H7)
    for (auto &gpio : kUlpiGpios) {
      gpio.config(GpioDefine::Mode::AlternateFunctionPushPull,
                  GpioDefine::Pull::NoPull, GpioDefine::Speed::VeryHigh,
                  kUlpiAlternate);
      gpio.init();
    }

    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct;
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USB;
    PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_PLL;
    HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct);

    __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
    __HAL_RCC_USB_OTG_HS_ULPI_CLK_ENABLE();
#endif
  } else if (_speed == USBSpeed::UsbHighSpeedInternalPHY &&
             _peripheral == USBPeripheral::UsbDifferencePair) {

  } else if (_speed == USBSpeed::UsbFullSpeed &&
             _peripheral == USBPeripheral::UsbDifferencePair) {
  } else {
    _initialized = false;
  }

  _initialized = true;
}

} // namespace USB