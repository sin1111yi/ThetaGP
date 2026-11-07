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
 * You should have received a copy of the GNU General
 * Public License along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "sched/port.h"

#include "build_info.h"

#include "drivers/device/system_timer.h"
#include "drivers/peripherals/systick.h"
#include "utils/atomic.h"

namespace ThetaGP::Sched {

namespace {

// The mask the interrupted code held, and the clocks its claim started at.
// The core brackets one claim at a time, so one set of values is enough.
// PRIMASK is used rather than BASEPRI because WFI only wakes on an interrupt
// that can preempt the current execution priority: a SysTick masked by BASEPRI
// would leave the core asleep forever.
FAST_DATA_ZERO_INIT uint32_t s_savedPrimask = 0;
FAST_DATA_ZERO_INIT uint32_t s_baseUs = 0;
FAST_DATA_ZERO_INIT uint32_t s_baseCycles = 0;

uint32_t portNowUs(void *) {
  return Drivers::Device::SystemTimer::getInstance().getMicros();
}

// The clock a claim may read: the free-running cycle counter, offset from the
// value the claim started at. It never waits on an interrupt the claim masked.
uint32_t portNowIsr(void *) {
  const uint32_t cycles = clockCycleCounter();
  const int32_t elapsed = static_cast<int32_t>(cycles - s_baseCycles);
  return s_baseUs + static_cast<uint32_t>(clockCyclesToMicros(elapsed));
}

void portEnter(void *) {
  s_baseUs = Drivers::Device::SystemTimer::getInstance().getMicros();
  s_baseCycles = clockCycleCounter();
  s_savedPrimask = __get_PRIMASK();
  __disable_irq();
}

void portExit(void *) { __set_PRIMASK(s_savedPrimask); }

void portIdle(void *, uint32_t deadline_us) {
  // Called with the claim's mask held, so only the free-running clock is read.
  // WFI wakes on a pending interrupt even with PRIMASK set; the handler runs
  // when the claim releases it.
  const uint32_t now = portNowIsr(nullptr);
  const int32_t delta = static_cast<int32_t>(deadline_us - now);
  if (delta <= 0) {
    return;
  }
  // WFI returns on the next interrupt, and the next SysTick is at most one
  // period away: a deadline nearer than that edge would pass while asleep.
  const uint32_t toTick =
      static_cast<uint32_t>(clockCyclesToMicros(static_cast<int32_t>(SysTick->VAL)));
  if (static_cast<uint32_t>(delta) < toTick) {
    return;
  }
  __WFI();
}

} // namespace

Port system_port() {
  Port port;
  port.now_us = portNowUs;
  port.now_us_isr = portNowIsr;
  port.enter_critical = portEnter;
  port.exit_critical = portExit;
  port.idle_until_us = portIdle;
  return port;
}

} // namespace ThetaGP::Sched
