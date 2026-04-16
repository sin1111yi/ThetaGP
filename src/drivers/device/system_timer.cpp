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

#include "drivers/device/system_timer.h"
#include "drivers/peripherals/systick.h"

namespace ThetaGP::Drivers::Device {

void SystemTimer::init() {
  if (_initialized)
    return;
  cycleCounterInit();
  _initialized = true;
}

uint32_t SystemTimer::getMicros() const { return micros(); }

uint32_t SystemTimer::getMillis() const { return millis(); }

uint32_t SystemTimer::getCycleCounter() const { return clockCycleCounter(); }

uint32_t SystemTimer::microsToCycles(uint32_t micros) const {
  return clockMicrosToCycles(micros);
}

int32_t SystemTimer::cyclesToMicros(int32_t cycles) const {
  return clockCyclesToMicros(cycles);
}

float SystemTimer::cyclesToMicrosf(int32_t cycles) const {
  return clockCyclesToMicrosf(cycles);
}

void SystemTimer::delayMicroseconds(uint32_t us) const { delay_us(us); }

void SystemTimer::delayMilliseconds(uint32_t ms) const { delay_ms(ms); }

} // namespace ThetaGP::Drivers::Device
