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

#pragma once

#include "utils/utils.h"
#include "drivers/peripherals/nvic.h"
#include "drivers/peripherals/gpio.h"

#include <functional>

#define NVIC_PROIRITY_BASE_WIDTH (2)
#define NVIC_PROIRITY_SUB_WIDTH (4 - NVIC_PROIRITY_BASE_WIDTH)

#define EXTI_IRQ_GROUPS 7

#if defined(STM32H7)
#define EXTI_REG_IMR (EXTI_D1->IMR1)
#define EXTI_REG_PR (EXTI_D1->PR1)
#else
#define EXTI_REG_IMR (EXTI->IMR)
#define EXTI_REG_PR (EXTI->PR)
#endif

namespace ThetaGP {
namespace Drivers {
namespace Periph {

using namespace GPIO;

namespace NVIC_EXTI {

enum class NvicPriority : uint8_t {
  PriorityVeryHigh,
  PriorityHigh,
  PriorityMedium,
  PriorityLow,
  PriorityVeryLow
};

class NvicExti {
private:
  Gpio _gpio;
  Mode _triggerSrc;
  NvicPriority _priority;
  bool _initialized;

public:
  using ExtiCallback = std::function<void(NvicExti *self)>;
  ExtiCallback _callback;

  NvicExti();
  explicit NvicExti(PinDesc pinDesc, Mode triggerSrc,
                    NvicPriority priority);
  explicit NvicExti(Port port, Pin pin,
                    Mode triggerSrc, NvicPriority priority);

  void init();
  void setCallback(ExtiCallback cb);
  void release();
  void enable();
  void disable();

  bool isInitialized() const { return _initialized; }
  PinState read() const { return _gpio.read(); }
  void write(PinState state) { _gpio.write(state); }
  void set() { _gpio.set(); }
  void reset() { _gpio.reset(); }
  void toggle() { _gpio.toggle(); }
};

} // namespace NVIC_EXTI
} // namespace Periph
} // namespace Drivers
} // namespace ThetaGP
