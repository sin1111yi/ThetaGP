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
#include <cstdint>
#include <functional>

namespace ThetaGP::Drivers::Peripheral::TIMER {

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

// Timer callback type with context parameter
using TimerCallback = std::function<void(void* context)>;

class HardwareTimer {
private:
  struct TimerState {
    Instance instance;
    TIM_HandleTypeDef htim;
    bool initialized;
    bool running;
  };

  TimerState _state;
  void* _context;

  void enableClock() const;

public:
  TimerCallback callback;

  HardwareTimer();
  HardwareTimer(Instance instance);

  void config(Instance instance, uint32_t frequency);
  
  // Set callback with context
  void setCallback(TimerCallback cb, void* context = nullptr);
  
  // Set context separately
  void setContext(void* context) { _context = context; }
  void* getContext() const { return _context; }

  void init();
  void start();
  void stop();

  bool isInitialized() const { return _state.initialized; }
  bool isRunning() const { return _state.running; }
  Instance getInstance() const { return _state.instance; }

  TIM_HandleTypeDef* getHandle() { return &_state.htim; }
  const TIM_HandleTypeDef* getHandle() const { return &_state.htim; }
};

Instance getPreferredBasicTimer();
Instance getFallbackBasicTimer();

constexpr Instance Timer6 = Instance::Timer6;
constexpr Instance Timer7 = Instance::Timer7;

}  // namespace ThetaGP::Drivers::Peripheral::TIMER
