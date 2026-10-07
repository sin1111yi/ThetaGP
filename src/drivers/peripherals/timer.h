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

#include "drivers/peripherals/gpio.h"
#include "drivers/peripherals/nvic_exti.h"

#include <cstdint>

namespace ThetaGP::Drivers::Peripheral::DMA {
class DmaChannel;
}

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

// A timer channel an output can come out of, named as a board wires it. The
// platform brings the channel up; what the output carries is the caller's.
enum class TimerChannel : uint8_t {
  Tim1Ch4,
  None = 0xFF,
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

  // PWM output state: the channel the board wired, the DMA stream its duty
  // values reach the compare register by, and the carrier's period in ticks.
  DMA::DmaChannel *_pwmDma = nullptr;
  TimerChannel _pwmChannel = TimerChannel::None;
  uint16_t _periodTicks = 0;

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

  // ── PWM output ──
  // Brings `channel` up on `pin` as a PWM output whose carrier runs at
  // `frequency`, ready for the duty values startSequence() hands over.
  bool initPwm(TimerChannel channel, const GPIO::PinDesc &pin,
               uint32_t frequency);

  // The ticks one carrier period spans: a duty is a number of them.
  [[nodiscard]] uint16_t periodTicks() const { return _periodTicks; }

  // Writes the first duty to the compare register and transfers the rest, one
  // per period, by DMA. False if the channel is not up, the count does not fit
  // the caller's buffer, or a sequence is still running.
  bool startSequence(const uint16_t *duties, uint16_t count);

  [[nodiscard]] bool isBusy() const;

  void *getContext() const { return _context; }

  bool isInitialized() const { return _state.initialized; }
  bool isRunning() const { return _state.running; }
  Instance getInstance() const { return _state.instance; }

  void *getHandle() { return _halHandle; }
};

} // namespace ThetaGP::Drivers::Peripheral::TIMER
