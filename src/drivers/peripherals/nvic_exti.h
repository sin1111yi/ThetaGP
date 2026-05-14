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

#include "drivers/peripherals/gpio.h"
#include "drivers/peripherals/nvic.h"

#define NVIC_PROIRITY_BASE_WIDTH (2)
#define NVIC_PROIRITY_SUB_WIDTH  (4 - NVIC_PROIRITY_BASE_WIDTH)

namespace ThetaGP::Drivers::Peripheral::NVIC_EXTI {

using namespace Peripheral::GPIO;

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
  bool _initialized = false;

  void *_context = nullptr;

  // ── C-style ISR callback ──
  typedef void (*ExtiCallback)(void *context);
  ExtiCallback _callback;

public:
  NvicExti();
  explicit NvicExti(PinDesc pinDesc, Mode triggerSrc, NvicPriority priority);
  explicit NvicExti(Port port, Pin pin, Mode triggerSrc, NvicPriority priority);

  static void preinit();
  void init();
  void setCallback(ExtiCallback cb, void *context = nullptr);
  void callback() {
    if (_callback) {
      _callback(_context);
    }
  }
  void release();
  void enable();
  void disable();

  void* getContext() const { return _context; }

  bool isInitialized() const { return _initialized; }
  PinState read() const { return _gpio.read(); }
  void write(PinState state) { _gpio.write(state); }
  void set() { _gpio.set(); }
  void reset() { _gpio.reset(); }
  void toggle() { _gpio.toggle(); }
};

} // namespace ThetaGP::Drivers::Peripheral::NVIC_EXTI
