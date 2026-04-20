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

#include "build_info.h"
#include "drivers/peripherals/nvic.h"
#include "drivers/peripherals/nvic_exti.h"

#include <cstdint>
#include <functional>

namespace ThetaGP::Drivers::Peripheral::TIMER {

using TimerPriority = NVIC_EXTI::NvicPriority;

enum class Instance : uint8_t {
#if defined(STM32H7)
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
#endif
  TimerNone = 0xFF
};

enum class TriggerEvent {
  Reset = TIM_TRGO_RESET,
  Enable = TIM_TRGO_ENABLE, // CNT_EN
  Update = TIM_TRGO_UPDATE,
  OC1 = TIM_TRGO_OC1,
  OC1Ref = TIM_TRGO_OC1REF,
  OC2Ref = TIM_TRGO_OC2REF,
  OC3Ref = TIM_TRGO_OC3REF,
  OC4Ref = TIM_TRGO_OC4REF,
};

class HardwareTimer {
private:
  struct TimerState {
    Instance instance;
    TIM_HandleTypeDef htim;
    TimerPriority priority = TimerPriority::PriorityMedium;
    TriggerEvent triggerEvent = TriggerEvent::Reset;
    bool initialized;
    bool running;
    uint32_t targetFrequency = 0;
  } _state;

  void *_context;

  using TimerCallback = std::function<void(void *context)>;
  TimerCallback _callback;

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

  // Set callback with context
  void setCallback(TimerCallback cb, void *context = nullptr);
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

  TIM_HandleTypeDef *getHandle() { return &_state.htim; }
  const TIM_HandleTypeDef *getHandle() const { return &_state.htim; }
};

Instance getPreferredBasicTimer();
Instance getFallbackBasicTimer();

constexpr Instance Timer6 = Instance::Timer6;
constexpr Instance Timer7 = Instance::Timer7;

} // namespace ThetaGP::Drivers::Peripheral::TIMER
