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

#pragma once

#include "drivers/peripherals/nvic_exti.h"

#include <cstdint>

namespace ThetaGP::Drivers::Peripheral::TIMER {

using TimerPriority = NVIC_EXTI::NvicPriority;

// ── C-style ISR callback ──
typedef void (*TimerCallbackFunc)(void *context);

enum class Instance : uint8_t {
  Timer1,
  Timer2,
  Timer3,
  Timer4,
  Timer5,
  Timer6,
  Timer7,
  Timer8,
  Timer12,
  Timer13,
  Timer14,
  Timer15,
  Timer16,
  Timer17,
  TimerNone = 0xFF
};

enum class TriggerEvent {
  Reset,
  Enable,
  Update,
  OC1,
  OC1Ref,
  OC2Ref,
  OC3Ref,
  OC4Ref,
};

class HardwareTimer {
private:
  struct TimerState {
    Instance instance = Instance::TimerNone;
    TimerPriority priority = TimerPriority::PriorityMedium;
    TriggerEvent triggerEvent = TriggerEvent::Reset;
    bool initialized = false;
    bool running = false;
    uint32_t targetFrequency = 0;
  } _state;

  void *_halHandle = nullptr;
  void *_context = nullptr;

  TimerCallbackFunc _callback;

  void enableClock() const;
  uint32_t getTimerClock() const;
  void calculatePrescalerAndPeriod(uint32_t frequency);

public:
  HardwareTimer();
  HardwareTimer(Instance instance);

  void config(Instance instance, uint32_t frequency);
  void config(Instance instance, uint32_t frequency,
              NVIC_EXTI::NvicPriority prio);
  void config(Instance instance, uint32_t frequency,
              NVIC_EXTI::NvicPriority prio, TriggerEvent triggerEvent);

  static uint32_t toHalTriggerEvent(TriggerEvent evt);

  // Set callback with context
  void setCallback(TimerCallbackFunc cb, void *context = nullptr);
  void callback() {
    if (_callback) {
      _callback(_context);
    }
  }

  void init();
  void start();
  void stop();

  void *getContext() const { return _context; }

  bool isInitialized() const { return _state.initialized; }
  bool isRunning() const { return _state.running; }
  Instance getInstance() const { return _state.instance; }

  void *getHandle() { return _halHandle; }
};

} // namespace ThetaGP::Drivers::Peripheral::TIMER
