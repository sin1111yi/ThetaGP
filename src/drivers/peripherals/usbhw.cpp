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

#include "drivers/peripherals/nvic.h"
#include "drivers/peripherals/nvic_exti.h"
#include "drivers/peripherals/systick.h"
#include "drivers/peripherals/usbhw.h"

#include "tusb.h"

#include <array>

using namespace ThetaGP::Drivers::Peripheral::USB;
using namespace ThetaGP::Drivers::Peripheral::GPIO;

#if defined(STM32H7)
// ULPI pin descriptors, index corresponds to ULPI enum value
static constexpr std::array<PinDesc, 12> ULPIPinDescs = {{
    {Port::PortA, Pin::Pin5},  // CLK, PA5
    {Port::PortC, Pin::Pin0},  // STP, PC0
    {Port::PortC, Pin::Pin2},  // DIR, PC2_C
    {Port::PortC, Pin::Pin3},  // NXT, PC3_C
    {Port::PortA, Pin::Pin3},  // D0, PA3
    {Port::PortB, Pin::Pin0},  // D1, PB0
    {Port::PortB, Pin::Pin1},  // D2, PB1
    {Port::PortB, Pin::Pin10}, // D3, PB10
    {Port::PortB, Pin::Pin11}, // D4, PB11
    {Port::PortB, Pin::Pin12}, // D5, PB12
    {Port::PortB, Pin::Pin13}, // D6, PB13
    {Port::PortB, Pin::Pin5},  // D7, PB5
}};

static constexpr uint32_t GpioAlternateULPI = GPIO_AF10_OTG2_HS; // 0x0A

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

HardwareUSB::HardwareUSB(USBSpeed speed, USBPeripheral peripheral)
    : _initialized(false), _speed(speed), _peripheral(peripheral) {}

void HardwareUSB::initULPIPins() {
#if defined(STM32H7)
  HAL_PWREx_EnableUSBVoltageDetector();
  for (const auto &pinDesc : ULPIPinDescs) {
    Gpio gpio(pinDesc);
    gpio.config(Mode::AlternateFunctionPushPull, Pull::NoPull, Speed::VeryHigh,
                GpioAlternateULPI);
    gpio.init();
  }

  __HAL_RCC_USB1_OTG_HS_CLK_ENABLE();
  __HAL_RCC_USB1_OTG_HS_ULPI_CLK_ENABLE();

  using Priority = NVIC_EXTI::NvicPriority;
#define PRIORITY(prio) static_cast<uint32_t>(Priority::prio)

  HAL_NVIC_SetPriority(OTG_HS_IRQn,
                       NVIC_PRIORITY_BASE(PRIORITY(PriorityVeryHigh)),
                       NVIC_PRIORITY_SUB(PRIORITY(PriorityVeryHigh)));
  HAL_NVIC_EnableIRQ(OTG_HS_IRQn);

#undef PRIORITY
#endif
}

void HardwareUSB::initHighSpeedPins() {
#if defined(STM32H7)
  Gpio dp(Port::PortA, Pin::Pin12);
  Gpio dm(Port::PortA, Pin::Pin11);

  dp.config(Mode::AlternateFunctionPushPull, Pull::NoPull, Speed::VeryHigh,
            GpioAlternateULPI);
  dm.config(Mode::AlternateFunctionPushPull, Pull::NoPull, Speed::VeryHigh,
            GpioAlternateULPI);

  dp.init();
  dm.init();

  __HAL_RCC_USB1_OTG_HS_CLK_ENABLE();
#endif
}

void HardwareUSB::initFullSpeedPins() {
#if defined(STM32H7) & 0
  Gpio dp(Port::PortA, Pin::Pin12);
  Gpio dm(Port::PortA, Pin::Pin11);

  dp.config(Mode::AlternateFunctionPushPull, Pull::NoPull, Speed::VeryHigh,
            kUlpiAlternate);
  dm.config(Mode::AlternateFunctionPushPull, Pull::NoPull, Speed::VeryHigh,
            kUlpiAlternate);

  dp.init();
  dm.init();

  __HAL_RCC_USB2_OTG_FS_CLK_ENABLE();
#endif
}

ThetaGP::RetVal HardwareUSB::init() {

  enableClock();

  if (_speed == USBSpeed::UsbHighSpeedExternalPHY &&
      _peripheral == USBPeripheral::ULPI) {
    initULPIPins();
  } else if (_speed == USBSpeed::UsbFullSpeed &&
             _peripheral == USBPeripheral::DifferenceLine) {
    initFullSpeedPins();
  } else if (_speed == USBSpeed::UsbHighSpeedInternalPHY ||
             _peripheral == USBPeripheral::DifferenceLine) {
    initHighSpeedPins();
  }

  return RetVal::Ok;
}

#ifdef __cplusplus
extern "C" {
#endif

uint32_t tusb_time_millis_api(void) { return millis(); }

#ifdef __cplusplus
}
#endif
