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

#include "stm32h7xx.h"
#include "stm32h7xx_hal_def.h"
#include "utils/types.h"

#include "drivers/peripherals/usb/usb.h"

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

static constexpr uint32_t kUlpiAlternate = GPIO_AF10_OTG2_HS; // 0x0A

static PCD_HandleTypeDef pcdHandler;

retval_t USB::initPCD() {
  if (_speed == USBSpeed::UsbHighSpeedExternalPHY &&
      _peripheral == USBPeripheral::UsbULPI) {
    pcdHandler.Instance = USB_OTG_HS;
    pcdHandler.Init.dev_endpoints = 9;
    pcdHandler.Init.speed = PCD_SPEED_HIGH;
    pcdHandler.Init.dma_enable = DISABLE;
    pcdHandler.Init.phy_itface = USB_OTG_ULPI_PHY;
    pcdHandler.Init.Sof_enable = DISABLE;
    pcdHandler.Init.low_power_enable = DISABLE;
    pcdHandler.Init.lpm_enable = DISABLE;
    pcdHandler.Init.vbus_sensing_enable = DISABLE;
    pcdHandler.Init.use_dedicated_ep1 = DISABLE;
    pcdHandler.Init.use_external_vbus = DISABLE;
    if (HAL_PCD_Init(&pcdHandler) != HAL_OK) {
      return rvFailed;
    }
  }

  return rvSucceed;
}

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
#if defined(STM32H7) & 0
  Gpio dp(Port::PortA, Pin::Pin12);
  Gpio dm(Port::PortA, Pin::Pin11);

  dp.config(Mode::AlternateFunctionPushPull, Pull::NoPull, Speed::VeryHigh,
            kUlpiAlternate);
  dm.config(Mode::AlternateFunctionPushPull, Pull::NoPull, Speed::VeryHigh,
            kUlpiAlternate);

  dp.init();
  dm.init();

  RCC_PeriphCLKInitTypeDef periphClkInitStruct = {0};
  periphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USB;
  periphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_PLL;
  HAL_RCCEx_PeriphCLKConfig(&periphClkInitStruct);

  __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
#endif
}

void USB::initFullSpeedPins() {
#if defined(STM32H7) & 0
  Gpio dp(Port::PortA, Pin::Pin12);
  Gpio dm(Port::PortA, Pin::Pin11);

  dp.config(Mode::AlternateFunctionPushPull, Pull::NoPull, Speed::VeryHigh,
            kUlpiAlternate);
  dm.config(Mode::AlternateFunctionPushPull, Pull::NoPull, Speed::VeryHigh,
            kUlpiAlternate);

  dp.init();
  dm.init();

  RCC_PeriphCLKInitTypeDef periphClkInitStruct = {0};
  periphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USB;
  periphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_PLL;
  HAL_RCCEx_PeriphCLKConfig(&periphClkInitStruct);

  __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
#endif
}

retval_t USB::init() {

  if (initPCD() != rvSucceed) {
    return rvFailed;
  }

  if (_speed == USBSpeed::UsbHighSpeedExternalPHY &&
      _peripheral == USBPeripheral::UsbULPI) {
    initULPIPins();
  } else if (_speed == USBSpeed::UsbFullSpeed &&
             _peripheral == USBPeripheral::USBDifferenceLine) {
    initFullSpeedPins();
  } else if (_speed == USBSpeed::UsbHighSpeedInternalPHY ||
             _peripheral == USBPeripheral::USBDifferenceLine) {
    initHighSpeedPins();
  }

  return rvSucceed;
}

} // namespace ThetaGP::Drivers::Periph::USB
