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

// The application layer's storage and its 100 Hz tick. The render itself is in
// led_effect.cpp, which carries no platform headers; this file holds the state
// that needs them, so the strip's buffer and the request flag live here.

#include "drivers/led/led_effect.h"

#include "build_info.h"
#include "conf/ThetaGP_Config.h" // THETAGP_CFG_LED_EFFECT_PERIOD_US, the cycle the frames span

namespace ThetaGP::Drivers::Led {
namespace {

// ── Frame buffer ──
// 2,048 elements, 6,144 B, of which FRAME_COUNT x LED_COUNT = 1,600 are
// written. AXI SRAM (COMMON_ZERO_INIT): the strip's driver hands this array to
// DMA, which cannot reach DTCM. Zero-initialised, so the strip stays dark until
// the first render.
COMMON_ZERO_INIT Rgb s_ledPixelBuf[LED_BUFFER_ELEMENTS]{};

static_assert(
    sizeof(s_ledPixelBuf) == LED_BUFFER_ELEMENTS * sizeof(Rgb),
    "the LED frame buffer no longer has LED_BUFFER_ELEMENTS elements");

// The LEDs a refresh request names, one bit per LED, bit index being LED
// index. A key callback writes it (producer), the task reads it and clears it
// (consumer). A non-zero mask is the request itself: the render covers the
// whole strip, so two requests that race cost nothing — whichever mask the
// task reads, every LED is rendered from it.
COMMON_ZERO_INIT volatile uint32_t s_requestedMask;

// The animation's clock and the hue its first frame starts at. Task context
// only.
LedEffectClock s_clock;
static constexpr uint16_t kStartHue = 0; // red

// Timestamp of the tick before the last one, for the elapsed time the
// animation advances by.
static uint32_t s_lastTickUs = 0;

// The frame interval is the cycle divided by FRAME_COUNT, and a frame has to
// stay on the strip for at least one tick of the task that advances it.
static_assert(THETAGP_CFG_LED_EFFECT_PERIOD_US / FRAME_COUNT >=
                  THETAGP_CFG_LED_TASK_PERIOD_US,
              "the LED effect's cycle divided by FRAME_COUNT is the time one "
              "frame is shown, and it has to cover at least one period of the "
              "task that advances the frames. Lengthen "
              "THETAGP_CFG_LED_EFFECT_PERIOD_US or shorten "
              "THETAGP_CFG_LED_TASK_PERIOD_US in src/conf/ThetaGP_Config.h.");

} // namespace

void ledEffectRequestRefresh(uint32_t ledMask) {
  s_requestedMask = s_requestedMask | ledMask;
}

void ledEffectTask(uint32_t currentTimeUs) {
  const uint32_t deltaUs = currentTimeUs - s_lastTickUs;
  s_lastTickUs = currentTimeUs;

  // The request is consumed before the render, so a request that lands during
  // the render is served by the tick after this one.
  const uint32_t requested = s_requestedMask;
  if (requested != 0) {
    s_requestedMask = 0;
    ledEffectRender(s_ledPixelBuf, LED_COUNT, THETAGP_CFG_LED_EFFECT_PERIOD_US,
                    kStartHue);
    s_clock = LedEffectClock{};
  }

  ledEffectAdvance(s_clock, deltaUs, THETAGP_CFG_LED_EFFECT_PERIOD_US);
}

uint8_t ledEffectCurrentFrame() { return s_clock.frame; }

Rgb *ledEffectFramebuffer() { return s_ledPixelBuf; }

} // namespace ThetaGP::Drivers::Led
