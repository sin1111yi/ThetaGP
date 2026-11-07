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
#include "drivers/peripherals/nvic.h"
#include "utils/atomic.h"

namespace ThetaGP::Sched {

namespace {

// The mask the interrupted code held, put back when the claim ends. The core
// brackets one claim at a time, so one saved value is enough.
FAST_DATA_ZERO_INIT uint8_t s_savedBasepri = 0;

uint32_t portNowUs(void *) {
  return Drivers::Device::SystemTimer::getInstance().getMicros();
}

void portEnter(void *) {
  s_savedBasepri = __get_BASEPRI();
  __set_BASEPRI(NVIC_PRIO_MAX);
}

void portExit(void *) { __set_BASEPRI(s_savedBasepri); }

// The nearest release a bare WFI can honour is one SysTick period: WFI returns
// on the next interrupt, which a 1 kHz tick places up to a millisecond later,
// so a nearer deadline is left to the caller's loop.
constexpr uint32_t kIdleFloorUs = 1000;

void portIdle(void *, uint32_t deadline_us) {
  Drivers::Device::SystemTimer &timer =
      Drivers::Device::SystemTimer::getInstance();
  const int32_t delta =
      static_cast<int32_t>(deadline_us - timer.getMicros());
  if (delta <= static_cast<int32_t>(kIdleFloorUs)) {
    return;
  }
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  if (static_cast<int32_t>(deadline_us - timer.getMicros()) > 0) {
    __WFI();
  }
  __set_PRIMASK(primask);
}

} // namespace

Port system_port() {
  Port port;
  port.now_us = portNowUs;
  port.enter_critical = portEnter;
  port.exit_critical = portExit;
  port.idle_until_us = portIdle;
  return port;
}

} // namespace ThetaGP::Sched
