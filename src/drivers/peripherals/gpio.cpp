/**
 * This file is a part of ThetaGP.
 *
 * ThetaGP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ThetaGP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "drivers/peripherals/gpio.h"
#include "stm32h743xx.h"

#include <array>
#include <memory>

namespace ThetaGP::Drivers::Peripheral::GPIO {

void enableGpioClock(Port port) {
  using ClockFunc = void (*)();
  static const std::array<ClockFunc, 11> clockEnableTable = {{
#if defined(STM32H7)
      []() { __HAL_RCC_GPIOA_CLK_ENABLE(); },
      []() { __HAL_RCC_GPIOB_CLK_ENABLE(); },
      []() { __HAL_RCC_GPIOC_CLK_ENABLE(); },
      []() { __HAL_RCC_GPIOD_CLK_ENABLE(); },
      []() { __HAL_RCC_GPIOE_CLK_ENABLE(); },
      []() { __HAL_RCC_GPIOF_CLK_ENABLE(); },
      []() { __HAL_RCC_GPIOG_CLK_ENABLE(); },
      []() { __HAL_RCC_GPIOH_CLK_ENABLE(); },
#if defined(GPIOI)
      []() { __HAL_RCC_GPIOI_CLK_ENABLE(); },
#else
      []() {},
#endif
      []() { __HAL_RCC_GPIOJ_CLK_ENABLE(); },
      []() { __HAL_RCC_GPIOK_CLK_ENABLE(); },
#endif
  }};

  const auto index = static_cast<size_t>(port);
  if (index < clockEnableTable.size()) {
    clockEnableTable[index]();
  }
}

Gpio::Gpio() {}

Gpio::Gpio(const PinDesc &pinDesc) {
  _config.port = pinDesc.port;
  _config.pin = pinDesc.pin;
}

Gpio::Gpio(Port port, Pin pin) {
  _config.port = port;
  _config.pin = pin;
}

void *Gpio::getPortAddress() const {
#if defined(STM32H7)
  constexpr uint32_t GPIO_PORT_BASE = GPIOA_BASE;
  constexpr uint32_t GPIO_PORT_OFFSET = 0x400;
  return reinterpret_cast<void *>(
      GPIO_PORT_BASE +
      (static_cast<uint32_t>(_config.port) * GPIO_PORT_OFFSET));
#endif
  return nullptr;
}

void *Gpio::getPortAddress(const PinDesc &pinDesc) {
#if defined(STM32H7)
  constexpr uint32_t GPIO_PORT_BASE = GPIOA_BASE;
  constexpr uint32_t GPIO_PORT_OFFSET = 0x400;
  return reinterpret_cast<void *>(
      GPIO_PORT_BASE +
      (static_cast<uint32_t>(pinDesc.port) * GPIO_PORT_OFFSET));
#endif
  return nullptr;
}

uint16_t Gpio::getPinMask() const {
#if defined(STM32H7)
  return static_cast<uint16_t>(1U << static_cast<uint8_t>(_config.pin));
#endif
  return 0x0000;
}

uint16_t Gpio::getPinMask(const PinDesc &pinDesc) {
#if defined(STM32H7)
  return static_cast<uint16_t>(1U << static_cast<uint8_t>(pinDesc.pin));
#endif
  return 0x0000;
}

void Gpio::enableClock() const { enableGpioClock(_config.port); }
void Gpio::enableClock(const PinDesc &pinDesc) { enableGpioClock(pinDesc.port); }

void Gpio::config(Mode mode, Pull pull, Speed speed) {
  _config.mode = mode;
  _config.pull = pull;
  _config.speed = speed;
  _config.alternate = 0;
}

void Gpio::config(Mode mode, Pull pull, Speed speed, uint32_t alternate) {
  config(mode, pull, speed);
  _config.alternate = alternate;
}

void Gpio::init() {
  enableClock();

#if defined(STM32H7)
  GPIO_InitTypeDef gpioInit{.Pin = getPinMask(),
                            .Mode = static_cast<uint32_t>(_config.mode),
                            .Pull = static_cast<uint32_t>(_config.pull),
                            .Speed = static_cast<uint32_t>(_config.speed),
                            .Alternate = _config.alternate};

  HAL_GPIO_Init(reinterpret_cast<GPIO_TypeDef *>(getPortAddress()), &gpioInit);
#endif
  _initialized = true;
}

void Gpio::write(PinState state) {
#if defined(STM32H7)
  HAL_GPIO_WritePin(reinterpret_cast<GPIO_TypeDef *>(getPortAddress()),
                    getPinMask(), static_cast<GPIO_PinState>(state));
#endif
}

PinState Gpio::read() const {
#if defined(STM32H7)
  return static_cast<PinState>(HAL_GPIO_ReadPin(
      reinterpret_cast<GPIO_TypeDef *>(getPortAddress()), getPinMask()));
#endif
}

void Gpio::toggle() {
#if defined(STM32H7)
  HAL_GPIO_TogglePin(reinterpret_cast<GPIO_TypeDef *>(getPortAddress()),
                     getPinMask());
#endif
}

void Gpio::set() { write(PinState::Set); }

void Gpio::reset() { write(PinState::Reset); }

} // namespace ThetaGP::Drivers::Periph::GPIO
