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

#pragma once

#include <cstdint>

namespace ThetaGP::Led {

// One LED's colour, its fields in the order the strip's wire carries them:
// green, red, blue, most significant bit first.
struct Rgb {
  uint8_t g = 0;
  uint8_t r = 0;
  uint8_t b = 0;
};

// Hue units in one full turn: six sectors of 256, so a wheel's sector and its
// ramp come out of a shift and a mask.
static constexpr uint16_t HUE_CYCLE = 1536;

// Frames one animation cycle is rendered into.
static constexpr uint8_t FRAME_COUNT = 50;

// LEDs one strip carries: one per key of the pad's key table. A refresh request
// names LEDs one bit each, so the count stays inside the mask's 32 bits.
static constexpr uint8_t LED_COUNT = 32;

// Elements of the frame buffer: FRAME_COUNT frames of LED_COUNT LEDs (1,600)
// held in a 2,048-element array.
static constexpr uint16_t LED_BUFFER_ELEMENTS = 2048;

static_assert(
    FRAME_COUNT * LED_COUNT <= LED_BUFFER_ELEMENTS,
    "the frame buffer has to hold FRAME_COUNT frames of LED_COUNT LEDs");

// The colour at a hue. Saturation and value are at full scale, and a hue at or
// past HUE_CYCLE wraps to the start of the wheel, so a running hue sum can be
// handed in as it is. Pure: no state is read and none is written.
Rgb hueToRgb(uint16_t hue);

// Renders FRAME_COUNT frames of the rainbow. Frame f of a keyCount-long strip
// runs from out[f * keyCount] to out[f * keyCount + keyCount - 1], so the
// array holds 50 * keyCount elements and nothing outside that range is
// written. A key's hue is its position along the strip and a frame's is its
// position along the cycle: periodUs is the cycle the frames span, and the
// rainbow's advance of HUE_CYCLE / FRAME_COUNT per frame does not depend on
// it. Pure: reads no state, touches no hardware, allocates nothing.
void ledEffectRender(Rgb *out, uint8_t keyCount, uint32_t periodUs,
                     uint16_t phaseOffset);

// The animation's clock: the frame it is showing and the time a tick carried
// over. A tick adds deltaUs and shows a new frame every periodUs / FRAME_COUNT,
// so the remainder a tick leaves is spent on the tick after it and the frames
// do not drift. Pure.
struct LedEffectClock {
  uint32_t carriedUs = 0;
  uint8_t frame = 0;
};

void ledEffectAdvance(LedEffectClock &clock, uint32_t deltaUs,
                      uint32_t periodUs);

// ── Application layer: request, tick, buffer ──
// These read and write the module's own storage, so they stay off the host.

// Requests a refresh of the LEDs the mask names, one bit per LED with the bit
// index being the LED index. Called from a key callback: stores the mask and
// returns. A zero mask requests nothing.
void ledEffectRequestRefresh(uint32_t ledMask);

// The 100 Hz tick: renders the strip when a request is pending, then advances
// the animation by the time since the tick before it. Takes the scheduler's
// task signature.
void ledEffectTask(uint32_t currentTimeUs);

// The frame the animation is showing. The push layer sends that frame's window.
uint8_t ledEffectCurrentFrame();

// The frame buffer, 2,048 elements of one colour each.
Rgb *ledEffectFramebuffer();

} // namespace ThetaGP::Led
