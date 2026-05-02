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

#include "drivers/device/device.h"
#include <cstdint>

namespace ThetaGP::Drivers::Device {

class SystemTimer : public Device {
public:
  static SystemTimer &getInstance() {
    static SystemTimer instance;
    return instance;
  }

  void init() override;

  uint32_t getMicros() const;
  uint32_t getMillis() const;
  uint32_t getCycleCounter() const;

  uint32_t microsToCycles(uint32_t micros) const;
  int32_t cyclesToMicros(int32_t cycles) const;
  float cyclesToMicrosf(int32_t cycles) const;

  void sleepMicros(uint32_t us) const;
  void sleepMillis(uint32_t ms) const;

private:
  SystemTimer();
};

} // namespace ThetaGP::Drivers::Device
