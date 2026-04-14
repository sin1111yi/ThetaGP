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

#include "drivers/peripherals/timer.h"
#include "build_info.h"

#include <array>

using namespace ThetaGP::Drivers::Peripheral::TIMER;

// Static registry for ISR dispatch
static std::array<HardwareTimer *, 16> s_timerInstances = {};

const std::array<TIM_TypeDef *, 17> timerInstance = {
    TIM1,  TIM2,  TIM3,  TIM4,  TIM5,  TIM6,    TIM7,    TIM8,   TIM12,
    TIM13, TIM14, TIM15, TIM16, TIM17, nullptr, nullptr, nullptr // Placeholders
};

constexpr std::array<IRQn_Type, 17> timerIRQn = {
    TIM1_UP_IRQn,
    TIM2_IRQn,
    TIM3_IRQn,
    TIM4_IRQn,
    TIM5_IRQn,
    TIM6_DAC_IRQn,
    TIM7_IRQn,
    TIM8_UP_TIM13_IRQn,
    TIM8_BRK_TIM12_IRQn,
    TIM8_UP_TIM13_IRQn,
    TIM8_TRG_COM_TIM14_IRQn,
    TIM15_IRQn,
    TIM16_IRQn,
    TIM17_IRQn,
    static_cast<IRQn_Type>(-1), // Placeholder
    static_cast<IRQn_Type>(-1), // Placeholder
    static_cast<IRQn_Type>(-1), // Placeholder
};

void HardwareTimer::enableClock() const {
  using ClockFunc = void (*)();

#if defined(STM32H7)
  constinit static const std::array<ClockFunc, 17> clockEnableTable = {{
      []() { __HAL_RCC_TIM1_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM2_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM3_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM4_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM5_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM6_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM7_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM8_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM12_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM13_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM14_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM15_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM16_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM17_CLK_ENABLE(); },
      []() {},
      []() {},
      []() {},
  }};
#else
  constinit static const std::array<ClockFunc, 17> clockEnableTable = {{
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
  }};
#endif

  const auto index = static_cast<size_t>(_state.instance);
  if (index < clockEnableTable.size()) {
    clockEnableTable[index]();
  }
}

Instance getPreferredBasicTimer() {
#if defined(STM32H7)
  return Instance::Timer6;
#else
  return Instance::TimerNone;
#endif
}

Instance getFallbackBasicTimer() {
#if defined(STM32H7)
  return Instance::Timer7;
#else
  return Instance::TimerNone;
#endif
}

HardwareTimer::HardwareTimer() : _state{}, _context(nullptr) {
  _state.instance = Instance::TimerNone;
}

HardwareTimer::HardwareTimer(Instance instance) : HardwareTimer() {
  _state.instance = instance;
}

void HardwareTimer::config(Instance instance, uint32_t /* frequency */) {
  _state.instance = instance;

  _state.htim.Init.Prescaler = 0;
  _state.htim.Init.CounterMode = TIM_COUNTERMODE_UP;
  _state.htim.Init.Period = 0;
  _state.htim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  _state.htim.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  _state.htim.Init.RepetitionCounter = 0;
}

void HardwareTimer::setCallback(TimerCallback cb, void* context) {
  callback = std::move(cb);
  _context = context;
}

void HardwareTimer::init() {
  if (_state.initialized) {
    return;
  }

  enableClock();

#if defined(STM32H7)
  uint32_t timerClock;

  if (_state.instance == Instance::Timer6 ||
      _state.instance == Instance::Timer7) {
    timerClock = HAL_RCC_GetPCLK1Freq();
    if (RCC->D2CFGR & RCC_D2CFGR_D2PPRE1) {
      timerClock *= 2;
    }
  } else if (_state.instance == Instance::Timer1 ||
             _state.instance == Instance::Timer8) {
    timerClock = HAL_RCC_GetPCLK2Freq();
    if (RCC->D2CFGR & RCC_D2CFGR_D2PPRE2) {
      timerClock *= 2;
    }
  } else {
    timerClock = SystemCoreClock / 4;
  }

  uint32_t prescaler = (timerClock / 1000000) - 1;
  uint32_t period = 10000 - 1;

  const auto index = static_cast<size_t>(_state.instance);
  if (index >= timerInstance.size() || !timerInstance[index]) {
    return;
  }

  _state.htim.Instance = timerInstance[index];
  _state.htim.Init.Prescaler = prescaler;
  _state.htim.Init.Period = period;

  if (HAL_TIM_Base_Init(&_state.htim) != HAL_OK) {
    return;
  }

  if (index < s_timerInstances.size()) {
    s_timerInstances[index] = this;
  }

  IRQn_Type irqn = timerIRQn[index];
  if (static_cast<int>(irqn) < 0) {
    return;
  }

  NVIC_SetPriority(irqn, 5);
  NVIC_EnableIRQ(irqn);

  _state.initialized = true;
  _state.running = true;

  HAL_TIM_Base_Start_IT(&_state.htim);
#endif
}

void HardwareTimer::start() {
  if (!_state.initialized || _state.running) {
    return;
  }

  if (HAL_TIM_Base_Start_IT(&_state.htim) == HAL_OK) {
    _state.running = true;
  }
}

void HardwareTimer::stop() {
  if (!_state.running) {
    return;
  }

  HAL_TIM_Base_Stop_IT(&_state.htim);
  _state.running = false;
}

extern "C" {

#if defined(STM32H7)

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
  for (size_t i = 0; i < s_timerInstances.size(); i++) {
    HardwareTimer *timer = s_timerInstances[i];
    if (timer && timer->getHandle() == htim) {
      if (timer->callback) {
        timer->callback(timer->getContext());
      }
      return;
    }
  }
}

#endif
}
