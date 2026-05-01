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

#include "build_info.h"

#include "drivers/peripherals/gpio.h"

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

uint32_t Gpio::toHalMode(Mode mode) {
  static constexpr uint32_t map[] = {
    GPIO_MODE_INPUT, GPIO_MODE_OUTPUT_PP, GPIO_MODE_OUTPUT_OD,
    GPIO_MODE_AF_PP, GPIO_MODE_AF_OD, GPIO_MODE_ANALOG,
    GPIO_MODE_IT_RISING, GPIO_MODE_IT_FALLING, GPIO_MODE_IT_RISING_FALLING,
  };
  return map[static_cast<size_t>(mode)];
}

uint32_t Gpio::toHalPull(Pull pull) {
  static constexpr uint32_t map[] = {GPIO_NOPULL, GPIO_PULLUP, GPIO_PULLDOWN};
  return map[static_cast<size_t>(pull)];
}

uint32_t Gpio::toHalSpeed(Speed speed) {
  static constexpr uint32_t map[] = {
    GPIO_SPEED_FREQ_LOW, GPIO_SPEED_FREQ_MEDIUM,
    GPIO_SPEED_FREQ_HIGH, GPIO_SPEED_FREQ_VERY_HIGH,
  };
  return map[static_cast<size_t>(speed)];
}

uint32_t Gpio::toHalPinState(PinState state) {
  static constexpr GPIO_PinState map[] = {GPIO_PIN_RESET, GPIO_PIN_SET};
  return map[static_cast<size_t>(state)];
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
                            .Mode = toHalMode(_config.mode),
                            .Pull = toHalPull(_config.pull),
                            .Speed = toHalSpeed(_config.speed),
                            .Alternate = _config.alternate};

  HAL_GPIO_Init(reinterpret_cast<GPIO_TypeDef *>(getPortAddress()), &gpioInit);
#endif
  _initialized = true;
}

void Gpio::write(PinState state) {
#if defined(STM32H7)
  HAL_GPIO_WritePin(reinterpret_cast<GPIO_TypeDef *>(getPortAddress()),
                    getPinMask(), static_cast<GPIO_PinState>(toHalPinState(state)));
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
