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

#include <array>

namespace ThetaGP::Drivers::GPIO {

void enableGpioClock(Port port) {
  using ClockFunc = void (*)();
  static const std::array<ClockFunc, 11> clockEnableTable = {{
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
  }};

  const auto index = static_cast<size_t>(port);
  if (index < clockEnableTable.size()) {
    clockEnableTable[index]();
  }
}

Gpio::Gpio() : _initialized(false) {
  _config.port = Port::PortNone;
  _config.pin = Pin::PinNone;
  _config.mode = Mode::Input;
  _config.pull = Pull::NoPull;
  _config.speed = Speed::Low;
  _config.alternate = 0;
}

Gpio::Gpio(const PinDesc &pinDesc) : Gpio() {
  _config.port = pinDesc.port;
  _config.pin = pinDesc.pin;
  _initialized = false;
}

Gpio::Gpio(Port port, Pin pin) : Gpio() {
  _config.port = port;
  _config.pin = pin;
  _initialized = false;
}

GPIO_TypeDef *Gpio::getPortAddress() const {
  constexpr uint32_t GPIO_PORT_BASE = GPIOA_BASE;
  constexpr uint32_t GPIO_PORT_OFFSET = 0x400;
  return reinterpret_cast<GPIO_TypeDef *>(
      GPIO_PORT_BASE +
      (static_cast<uint32_t>(_config.port) * GPIO_PORT_OFFSET));
}

uint16_t Gpio::getPinMask() const {
  return static_cast<uint16_t>(1U << static_cast<uint8_t>(_config.pin));
}

void Gpio::enableClock() const { enableGpioClock(_config.port); }

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

  GPIO_InitTypeDef gpioInit{.Pin = getPinMask(),
                            .Mode = static_cast<uint32_t>(_config.mode),
                            .Pull = static_cast<uint32_t>(_config.pull),
                            .Speed = static_cast<uint32_t>(_config.speed),
                            .Alternate = _config.alternate};

  HAL_GPIO_Init(getPortAddress(), &gpioInit);
  reset();

  _initialized = true;
}

void Gpio::write(PinState state) {
  HAL_GPIO_WritePin(getPortAddress(), getPinMask(),
                    static_cast<GPIO_PinState>(state));
}

PinState Gpio::read() const {
  return static_cast<PinState>(
      HAL_GPIO_ReadPin(getPortAddress(), getPinMask()));
}

void Gpio::toggle() { HAL_GPIO_TogglePin(getPortAddress(), getPinMask()); }

void Gpio::set() { write(PinState::Set); }

void Gpio::reset() { write(PinState::Reset); }

}
