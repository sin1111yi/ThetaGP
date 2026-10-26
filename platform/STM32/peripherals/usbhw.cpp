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

#include "build_info.h"

#include "utils/types.h"
#include "utils/utils.h"

#include "drivers/peripherals/nvic.h"
#include "drivers/peripherals/nvic_exti.h"
#include "drivers/peripherals/systick.h"
#include "drivers/peripherals/usbhw.h"

#include "tusb.h"

#include <array>
#include <cstring>

using namespace ThetaGP::Drivers::Peripheral::USB;
using namespace ThetaGP::Drivers::Peripheral::GPIO;

#if defined(STM32H7)
static constexpr std::array<PinDesc, 2> DpDmPinDescs = {{
    {Port::PortA, Pin::Pin12},
    {Port::PortA, Pin::Pin11},
}};

static constexpr uint32_t GpioAlternateUSB = 0x0A;

extern "C" {
// Despite being call USB2_OTG_FS on some MCUs
// OTG_FS is marked as RHPort0 by TinyUSB to be consistent across stm32 port
void OTG_FS_IRQHandler(void) { tusb_int_handler(0, true); }

// Despite being call USB1_OTG_HS on some MCUs
// OTG_HS is marked as RHPort1 by TinyUSB to be consistent across stm32 port
void OTG_HS_IRQHandler(void) { tusb_int_handler(1, true); }
}

#endif

void HardwareUSB::enableClock() const {
#if defined(STM32H7)
  RCC_PeriphCLKInitTypeDef periphClkInitStruct;

  std::memset(&periphClkInitStruct, 0, sizeof(RCC_PeriphCLKInitTypeDef));

  periphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USB;

  if (_speed == USBSpeed::UsbFullSpeed) {
    // Internal 48MHz clock source for full-speed USB.
    periphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
  } else {
    // High-speed USB need a 60MHz clock source.
    periphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_PLL;
  }

  HAL_RCCEx_PeriphCLKConfig(&periphClkInitStruct);
#endif
}

HardwareUSB::HardwareUSB(USBSpeed speed, USBPeripheral peripheral) {
  _speed = speed;
  _peripheral = peripheral;
}

void HardwareUSB::initULPIPins() {
// Only a board that wires a PHY has the lines to drive: every other board
// compiles this body to nothing.
#if defined(STM32H7) && defined(BDCFG_ULPI_PINS)
  // The twelve lines the board wired to the PHY, in the order the ULPI port
  // drives them: the clock, the three handshakes, then the eight data bits.
  static constexpr std::array<PinDesc, BDCFG_ULPI_PIN_NUM> ULPIPinDescs = {{
      BDCFG_ULPI_PINS}};
  static_assert(BDCFG_ULPI_PIN_NUM == 12,
                "usb.ulpi_pins: the PHY is wired with twelve pins (CLK, STP, "
                "DIR, NXT, D0..D7)");

  // Enable SYSCFG IO compensation cell for ULPI 60MHz signal integrity
  HAL_EnableCompensationCell();

  for (const auto &pinDesc : ULPIPinDescs) {
    Gpio gpio(pinDesc);
    gpio.config(Mode::AlternateFunctionPushPull, Pull::NoPull, Speed::VeryHigh,
                GpioAlternateUSB);
    gpio.init();
  }

  __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
  __HAL_RCC_USB_OTG_HS_ULPI_CLK_ENABLE();

  using Priority = NVIC_EXTI::NvicPriority;
#define PRIORITY(prio) static_cast<uint32_t>(Priority::prio)

  HAL_NVIC_SetPriority(OTG_HS_IRQn,
                       NVIC_PRIORITY_BASE(PRIORITY(PriorityVeryHigh)),
                       NVIC_PRIORITY_SUB(PRIORITY(PriorityVeryHigh)));
  //   HAL_NVIC_EnableIRQ(OTG_HS_IRQn);

#undef PRIORITY
#endif
}

void HardwareUSB::initHighSpeedPins() {
#if defined(STM32H7)
  for (const auto &pinDesc : DpDmPinDescs) {
    Gpio gpio(pinDesc);
    gpio.config(Mode::AlternateFunctionPushPull, Pull::NoPull, Speed::VeryHigh,
                GpioAlternateUSB);
    gpio.init();
  }

  __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
#endif
}

void HardwareUSB::initFullSpeedPins() {
#if defined(STM32H7)
  for (const auto &pinDesc : DpDmPinDescs) {
    Gpio gpio(pinDesc);
    gpio.config(Mode::AlternateFunctionPushPull, Pull::NoPull, Speed::VeryHigh,
                GpioAlternateUSB);
    gpio.init();
  }

  __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
#endif
}

ThetaGP::Result HardwareUSB::init() {
  enableClock();

  HAL_PWREx_EnableUSBVoltageDetector();
  /* clang-format off */
  if (_peripheral == USBPeripheral::ULPI && 
      _speed == USBSpeed::UsbHighSpeed) {
    initULPIPins();
  } else if (_peripheral == USBPeripheral::OTG1 &&
             _speed == USBSpeed::UsbHighSpeed) {
    initHighSpeedPins();
  } else if (_peripheral == USBPeripheral::OTG2 &&
             _speed == USBSpeed::UsbFullSpeed) {
    initFullSpeedPins();
  }
  /* clang-format on */

  return Result::Ok;
}

#ifdef __cplusplus
extern "C" {
#endif

uint32_t tusb_time_millis_api(void) { return millis(); }

#ifdef __cplusplus
}
#endif
