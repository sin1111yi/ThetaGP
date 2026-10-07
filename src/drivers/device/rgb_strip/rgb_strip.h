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
#include "drivers/peripherals/timer.h"

#include <cstdint>

namespace ThetaGP::Drivers::Device {

namespace LedEffect {
struct Rgb;
}

// The strip the board wires to a timer channel, one LED per pixel: the rainbow
// is rendered once into a frame buffer, and each frame goes out as the bit
// stream a WS2812 strip reads.
class RgbStrip : public Device {
public:
  static RgbStrip &getInstance() {
    static RgbStrip instance;
    return instance;
  }

  void init() override;

  // The scheduler's tick: advance the animation and send the frame it lands on.
  static void task(uint32_t currentTimeUs);

private:
  RgbStrip() : Device("rgb_strip") {}

  // The frame's bits as one duty per carrier period, then the reset.
  bool send(const LedEffect::Rgb *frame);
  void encode(const LedEffect::Rgb *frame);

  Peripheral::TIMER::HardwareTimer _timer;
  uint16_t _zeroDuty = 0;
  uint16_t _oneDuty = 0;
};

} // namespace ThetaGP::Drivers::Device
