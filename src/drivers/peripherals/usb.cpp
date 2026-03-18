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

#include "drivers/peripherals/usb.h"
#include "drivers/peripherals/gpio.h"

#include <array>

namespace ThetaGP::Drivers::USB {

using namespace GPIO;

#if defined(STM32H7)

// ULPI GPIO array, index corresponds to UlpiPin enum value
std::array<Gpio, 12> kUlpiGpios = {{
    Gpio({Port::PortA, Pin::Pin5}),  // CLK
    Gpio({Port::PortC, Pin::Pin0}),  // STP
    Gpio({Port::PortC, Pin::Pin2}),  // DIR
    Gpio({Port::PortC, Pin::Pin3}),  // NXT
    Gpio({Port::PortA, Pin::Pin3}),  // D0
    Gpio({Port::PortB, Pin::Pin0}),  // D1
    Gpio({Port::PortB, Pin::Pin1}),  // D2
    Gpio({Port::PortB, Pin::Pin10}), // D3
    Gpio({Port::PortB, Pin::Pin11}), // D4
    Gpio({Port::PortB, Pin::Pin12}), // D5
    Gpio({Port::PortB, Pin::Pin13}), // D6
    Gpio({Port::PortB, Pin::Pin5}),  // D7
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
      gpio.config(Mode::AlternateFunctionPushPull, Pull::NoPull,
                  Speed::VeryHigh, kUlpiAlternate);
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

} // namespace ThetaGP::Drivers::USB
