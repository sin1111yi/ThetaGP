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

// The colour and animation arithmetic. Nothing platform-specific is reachable
// from here: no memory-region macro, no driver, no configuration. A host
// compiler builds this translation unit as it stands, which is what lets
// scripts/test/test_led_effect.py drive the firmware's own render on the host.

#include "led/led_effect.h"

namespace ThetaGP::Led {
namespace {

// The wheel in six sectors of 256 hue units, each a ramp on one channel: the
// sector names the channel that rises across it, the one that falls across it,
// and the one it holds at full scale. The remaining channel sits at zero.
enum Channel : uint8_t { Green = 0, Red = 1, Blue = 2 };

// The value in a sector's `up` or `down` slot for a channel the sector does not
// ramp at all.
static constexpr uint8_t NoRamp = 0xFF;

struct Sector {
  uint8_t up;
  uint8_t down;
  uint8_t full;
};

// Sector index is the hue's 256-unit block: red to yellow, yellow to green,
// green to cyan, cyan to blue, blue to magenta, magenta to red.
static constexpr Sector WHEEL[HUE_CYCLE / 256] = {
    {Green, NoRamp, Red},  // red -> yellow
    {NoRamp, Red, Green},  // yellow -> green
    {Blue, NoRamp, Green}, // green -> cyan
    {NoRamp, Green, Blue}, // cyan -> blue
    {Red, NoRamp, Blue},   // blue -> magenta
    {NoRamp, Blue, Red},   // magenta -> red
};

} // namespace

Rgb hueToRgb(uint16_t hue) {
  const uint16_t turn = static_cast<uint16_t>(hue % HUE_CYCLE);
  const uint8_t ramp = static_cast<uint8_t>(turn & 0xFF);
  const Sector &sector = WHEEL[turn >> 8];

  uint8_t channel[3] = {0, 0, 0};
  channel[sector.full] = 255;
  if (sector.up != NoRamp) {
    channel[sector.up] = ramp;
  }
  if (sector.down != NoRamp) {
    channel[sector.down] = static_cast<uint8_t>(255 - ramp);
  }

  // The channel indexes are the colour's fields in order: green, red, blue.
  return Rgb{channel[Green], channel[Red], channel[Blue]};
}

void ledEffectRender(Rgb *out, uint8_t keyCount, uint32_t periodUs,
                     uint16_t phaseOffset) {
  // The rainbow's hue advance is HUE_CYCLE / FRAME_COUNT per frame whatever
  // cycle the caller spans, so the period is not read here.
  static_cast<void>(periodUs);

  if (keyCount == 0) {
    return;
  }

  const uint16_t keyStep = static_cast<uint16_t>(HUE_CYCLE / keyCount);

  for (uint8_t frame = 0; frame < FRAME_COUNT; ++frame) {
    // Multiplied before divided: a per-frame increment would carry the
    // truncation of every earlier frame into the one after it.
    const uint16_t frameHue =
        static_cast<uint16_t>((frame * HUE_CYCLE) / FRAME_COUNT);

    for (uint8_t led = 0; led < keyCount; ++led) {
      const uint16_t hue =
          static_cast<uint16_t>(phaseOffset + led * keyStep + frameHue);
      out[frame * keyCount + led] = hueToRgb(hue);
    }
  }
}

void ledEffectAdvance(LedEffectClock &clock, uint32_t deltaUs,
                      uint32_t periodUs) {
  const uint32_t frameIntervalUs = periodUs / FRAME_COUNT;
  if (frameIntervalUs == 0) {
    return;
  }

  clock.carriedUs += deltaUs;
  while (clock.carriedUs >= frameIntervalUs) {
    clock.carriedUs -= frameIntervalUs;
    clock.frame = static_cast<uint8_t>((clock.frame + 1) % FRAME_COUNT);
  }
}

} // namespace ThetaGP::Led
