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

#include "drivers/peripherals/nvic_exti.h"

#include <array>

using namespace ThetaGP::Drivers::Peripheral::NVIC_EXTI;
using namespace ThetaGP::Drivers::Peripheral::GPIO;

std::array<NvicExti *, 16> extiInstances = {};

constexpr std::array<uint8_t, 16> extiGroups = {0, 1, 2, 3, 4, 5, 5, 5,
                                                5, 5, 6, 6, 6, 6, 6, 6};
std::array<uint8_t, EXTI_IRQ_GROUPS> extiGroupPriority = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

#if defined(STM32H7)
constexpr std::array<IRQn_Type, EXTI_IRQ_GROUPS> extiGroupIRQn = {
    EXTI0_IRQn, EXTI1_IRQn,   EXTI2_IRQn,    EXTI3_IRQn,
    EXTI4_IRQn, EXTI9_5_IRQn, EXTI15_10_IRQn};
#else
#warning "Unknown CPU"
#endif

NvicExti::NvicExti()
    : _gpio(), _triggerSrc(Mode::Input), _priority(NvicPriority::PriorityLow),
      _initialized(false) {}

NvicExti::NvicExti(PinDesc pinDesc, Mode triggerSrc, NvicPriority priority)
    : _gpio(pinDesc), _triggerSrc(triggerSrc), _priority(priority),
      _initialized(false) {}

NvicExti::NvicExti(Port port, Pin pin, Mode triggerSrc, NvicPriority priority)
    : _gpio(port, pin), _triggerSrc(triggerSrc), _priority(priority),
      _initialized(false) {}

void NvicExti::preinit() {
#if defined(STM32H7)
  HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITY_GROUPING);
#endif
}

void NvicExti::init() {
  _gpio.config(_triggerSrc, Pull::PullUp, Speed::High);
  _gpio.init();

  const auto &cfg = _gpio.getConfig();
  const uint32_t pinIdx = static_cast<uint8_t>(cfg.pin);
  int group = extiGroups[pinIdx];

  if (pinIdx >= extiInstances.size()) {
    return;
  }

  extiInstances[pinIdx] = this;
  disable();

#if defined(STM32H7)
  GPIO_InitTypeDef gpioInit{.Pin = _gpio.getPinMask(),
                            .Mode = static_cast<uint32_t>(_triggerSrc) |
                                    static_cast<uint32_t>(Mode::Input) |
                                    static_cast<uint32_t>(cfg.mode),
                            .Pull = static_cast<uint32_t>(cfg.pull),
                            .Speed = static_cast<uint32_t>(cfg.speed),
                            .Alternate = cfg.alternate};

  HAL_GPIO_Init(reinterpret_cast<GPIO_TypeDef *>(_gpio.getPortAddress()),
                &gpioInit);

  if (extiGroupPriority[group] > static_cast<uint8_t>(_priority)) {
    extiGroupPriority[group] = static_cast<uint8_t>(_priority);
    HAL_NVIC_SetPriority(extiGroupIRQn[group],
                         NVIC_PRIORITY_BASE(static_cast<uint32_t>(_priority)),
                         NVIC_PRIORITY_SUB(static_cast<uint32_t>(_priority)));
    HAL_NVIC_EnableIRQ(extiGroupIRQn[group]);
  }
#endif

  _initialized = true;
}

void NvicExti::setCallback(ExtiCallback cb) { _callback = std::move(cb); }

void NvicExti::disable() {
#if defined(STM32H7)
  const auto &cfg = _gpio.getConfig();
  uint32_t extiLine = 1 << static_cast<uint8_t>(cfg.pin);

  if (!extiLine)
    return;

  EXTI_REG_IMR &= ~extiLine;
  EXTI_REG_PR = extiLine;
#else
#error "Unknown CPU"
#endif
}

void NvicExti::enable() {
#if defined(STM32H7)
  const auto &cfg = _gpio.getConfig();
  uint32_t extiLine = 1 << static_cast<uint8_t>(cfg.pin);

  if (!extiLine) {
    return;
  }

  EXTI_REG_IMR |= extiLine;
#else
#error "Unknown CPU"
#endif
}

void NvicExti::release() {
  disable();

  const auto &cfg = _gpio.getConfig();
  const uint32_t chIdx = static_cast<uint8_t>(cfg.pin);

  if (chIdx >= extiInstances.size()) {
    return;
  }

  extiInstances[chIdx] = nullptr;
}

// C code for ISR vector table
extern "C" {

#if defined(STM32H7)

#define EXTI_EVENT_MASK 0xFFFF

static void EXTI_IRQnHandler(uint32_t mask) {
  uint32_t exti_active = (EXTI_REG_IMR & EXTI_REG_PR) & mask;

  EXTI_REG_PR = exti_active;

  while (exti_active) {
    uint32_t idx = 31 - __builtin_clz(exti_active);
    uint32_t bit = 1U << idx;
    auto *extiInstance = extiInstances[idx];
    if (extiInstance && extiInstance->_callback) {
      extiInstance->_callback(extiInstance);
    }
    exti_active &= ~bit;
  }
}

#define extiIrqHandler(name, mask)                                             \
  void name(void) { EXTI_IRQnHandler(mask & EXTI_EVENT_MASK); }                \
  struct dummy

extiIrqHandler(EXTI0_IRQHandler, 0x0001);
extiIrqHandler(EXTI1_IRQHandler, 0x0002);
extiIrqHandler(EXTI2_IRQHandler, 0x0004);
extiIrqHandler(EXTI3_IRQHandler, 0x0008);
extiIrqHandler(EXTI4_IRQHandler, 0x0010);
extiIrqHandler(EXTI9_5_IRQHandler, 0x03e0);
extiIrqHandler(EXTI15_10_IRQHandler, 0xfc00);

#endif
}
