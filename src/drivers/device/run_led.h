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

#include "BoardConfig.h"
#include "build_info.h"
#include "drivers/device/device.h"
#include "drivers/peripherals/gpio.h"

namespace ThetaGP::Drivers::Device {

class RunLed : public Device {
public:
  enum class Effect : uint8_t {
    Manual,       // fixed state, set via setState()
    SlowBlink,    // 1 Hz
    FastBlink,    // 5 Hz
    DoubleFlash,  // two quick pulses, ~1s cycle
  };

  static RunLed &getInstance() {
    static RunLed instance;
    return instance;
  }

  void init() override;
  void update(uint32_t currentTimeUs);
  void setEffect(Effect effect);
  void setState(bool on);

private:
  struct Phase {
    bool state;         // LED on/off
    uint32_t durationUs; // phase duration in microseconds
  };

  static constexpr Phase EFFECT_TABLE[3][4] = {
    // SlowBlink (index 0 = Effect::SlowBlink - 1)
    {{true, 500000}, {false, 500000}},
    // FastBlink
    {{true, 100000}, {false, 100000}},
    // DoubleFlash
    {{true, 100000}, {false, 100000}, {true, 100000}, {false, 700000}},
  };

  RunLed();
  Peripheral::GPIO::Gpio _led;
  Effect _effect = Effect::SlowBlink;
  uint8_t _phaseIdx = 0;
  uint32_t _phaseStartUs = 0;
  bool _manualState = false;
};

} // namespace ThetaGP::Drivers::Device
