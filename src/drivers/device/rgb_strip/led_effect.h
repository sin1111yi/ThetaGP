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

#include <cstdint>

namespace ThetaGP::Drivers::Device::LedEffect {

// One LED's colour, its fields in the wire's order: green, red, blue.
struct Rgb {
  uint8_t g = 0;
  uint8_t r = 0;
  uint8_t b = 0;
};

// Hue units in one full turn: six sectors of 256.
static constexpr uint16_t HUE_CYCLE = 1536;

// Frames one animation cycle is rendered into.
static constexpr uint8_t FRAME_COUNT = 50;

// The colour at a hue, at full saturation and value. A hue at or past
// HUE_CYCLE wraps to the start of the wheel.
Rgb hueToRgb(uint16_t hue);

// Renders FRAME_COUNT frames of the rainbow into out, which holds 50 *
// ledCount elements: frame f of a ledCount-long strip runs from
// out[f * ledCount] to out[f * ledCount + ledCount - 1]. An LED's hue is its
// position along the strip, a frame's is its position along the cycle.
void render(Rgb *out, uint8_t ledCount, uint16_t phaseOffset);

// The frame the animation is showing and the time a tick carried over.
struct Clock {
  uint32_t carriedUs = 0;
  uint8_t frame = 0;
};

// A tick adds deltaUs and shows a new frame once it covers frameIntervalUs; the
// remainder it leaves is spent on the tick after it, so frames do not drift.
void advance(Clock &clock, uint32_t deltaUs, uint32_t frameIntervalUs);

} // namespace ThetaGP::Drivers::Device::LedEffect
