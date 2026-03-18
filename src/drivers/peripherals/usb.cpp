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

#include <array>

namespace ThetaGP::Drivers::Periph::USB {

using namespace GPIO;

#if defined(STM32H7)

// ULPI pin descriptors, index corresponds to ULPI enum value
static constexpr std::array<PinDesc, 12> kUlpiPinDescs = {{
    {Port::PortA, Pin::Pin5},  // CLK
    {Port::PortC, Pin::Pin0},  // STP
    {Port::PortC, Pin::Pin2},  // DIR
    {Port::PortC, Pin::Pin3},  // NXT
    {Port::PortA, Pin::Pin3},  // D0
    {Port::PortB, Pin::Pin0},  // D1
    {Port::PortB, Pin::Pin1},  // D2
    {Port::PortB, Pin::Pin10}, // D3
    {Port::PortB, Pin::Pin11}, // D4
    {Port::PortB, Pin::Pin12}, // D5
    {Port::PortB, Pin::Pin13}, // D6
    {Port::PortB, Pin::Pin5},  // D7
}};

static constexpr uint32_t kUlpiAlternate = GPIO_AF10_OTG2_HS;

#endif

USB::USB(USBSpeed speed, USBPeripheral peripheral)
    : _initialized(false), _speed(speed), _peripheral(peripheral) {}

void USB::initULPIPins() {
#if defined(STM32H7)
  for (const auto &pinDesc : kUlpiPinDescs) {
    Gpio gpio(pinDesc);
    gpio.config(Mode::AlternateFunctionPushPull, Pull::NoPull, Speed::VeryHigh,
                kUlpiAlternate);
    gpio.init();
  }

  RCC_PeriphCLKInitTypeDef periphClkInitStruct;
  periphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USB;
  periphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_PLL;
  HAL_RCCEx_PeriphCLKConfig(&periphClkInitStruct);

  __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
  __HAL_RCC_USB_OTG_HS_ULPI_CLK_ENABLE();
#endif
}

void USB::initHighSpeedPins() {
  // TODO: Implement Full Speed USB initialization
}

void USB::initFullSpeedPins() {
  // TODO: Implement Full Speed USB initialization
}

void USB::init() {
  bool success = false;

  if (_speed == USBSpeed::UsbHighSpeedExternalPHY &&
      _peripheral == USBPeripheral::UsbULPI) {
    initULPIPins();
    success = true;
  } else if (_speed == USBSpeed::UsbFullSpeed &&
             _peripheral == USBPeripheral::UsbDifferencePair) {

    initFullSpeedPins();
    success = true;
  } else if (_speed == USBSpeed::UsbHighSpeedInternalPHY ||
             _peripheral == USBPeripheral::UsbDifferencePair) {
    initHighSpeedPins();
    success = true;
  }

  _initialized = success;
}

} // namespace ThetaGP::Drivers::Periph::USB
