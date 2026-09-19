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

// Host cases for the LED effect (src/drivers/led/led_effect.cpp), run by
// scripts/test/test_led_effect.py. It links the firmware's own led_effect.cpp
// unchanged and calls ledEffectRender on the host, so a case here says what the
// firmware's render does and not what a stand-in for it does. The file carries
// no platform header, which is what lets it build off the board.
//
// What the cases pin down, one group each:
//
//   1. start colour — frame 0, LED 0 is the wheel colour at phaseOffset. The
//      expected values are the six primaries and two ramps written out by
//      hand, which fix the wheel's orientation as well as the entry hue.
//   2. LED step — neighbouring LEDs of one frame sit HUE_CYCLE / keyCount
//      apart, and the frame spans (keyCount - 1) of those steps. As in the
//      frame cases below, the pair tolerance alone lets a step that is one
//      unit out pass; the span multiplies each pair's error by the number of
//      steps, so the same defect the pair misses the span catches.
//   3. frame step — neighbouring frames sit HUE_CYCLE / FRAME_COUNT apart, and
//      the 49 steps from frame 0 to frame 49 span 49 of them. Adjacent pairs
//      alone cannot see a render that divides before multiplying: that render
//      keeps every pair inside the tolerance while the run shortens by the
//      truncation of every frame, and the span is what catches it.
//   4. full turn — a phaseOffset of p and one of p + HUE_CYCLE render the same
//      frame data, so a hue that passes the end of the wheel comes back to its
//      start instead of running off it.
//   5. keyCount boundaries — keyCount 0 writes nothing, and keyCount 1 and 32
//      write exactly FRAME_COUNT * keyCount elements and leave every element
//      behind that range as it was. The untouched check is what catches a
//      render whose stride or per-frame count ignores keyCount.
//   6. clock — the 100 Hz tick's frame advance, driven by hand: the carried
//      remainder is spent on the tick after it (two 12 ms ticks against a
//      20 ms frame interval show one frame, where a per-tick division shows
//      none), a cycle that divides evenly lands back on the frame it started
//      on with nothing carried, and a cycle under FRAME_COUNT does not advance.
//
// The colour the implementation returns is read back as the hue that produces
// it: the cases ask the wheel below which hue yields the colour they saw. That
// wheel is written a second way — six sectors naming the channel that rises,
// the one that falls and the one held at full scale — so it is not a reading of
// the implementation it checks. It also cannot see a one-unit hue error: a
// sector's two ends produce the same colour (hue 255 and 256 are both full
// yellow), so a boundary colour reads back as the lower of the two hues, which
// stays inside the +-1 the hue cases allow.
//
// Run: python3 scripts/test/test_led_effect.py
//      (compiles this file with src/drivers/led/led_effect.cpp into a temporary
//      directory and runs it; exit 0 = every case matched).

#include "drivers/led/led_effect.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using ThetaGP::Drivers::Led::FRAME_COUNT;
using ThetaGP::Drivers::Led::HUE_CYCLE;
using ThetaGP::Drivers::Led::hueToRgb;
using ThetaGP::Drivers::Led::LED_BUFFER_ELEMENTS;
using ThetaGP::Drivers::Led::LED_COUNT;
using ThetaGP::Drivers::Led::ledEffectAdvance;
using ThetaGP::Drivers::Led::LedEffectClock;
using ThetaGP::Drivers::Led::ledEffectRender;
using ThetaGP::Drivers::Led::Rgb;

namespace {

int g_checks = 0;
int g_failed = 0;

// ── The wheel, written a second way ──

enum Channel : uint8_t { Green = 0, Red = 1, Blue = 2 };

static constexpr uint8_t NoRamp = 0xFF;

struct Sector {
  uint8_t up;
  uint8_t down;
  uint8_t full;
};

// The same six 256-unit sectors the effect uses, reached by hand from the
// wheel's shape: red to yellow ramps green up, yellow to green ramps red down,
// green to cyan ramps blue up, cyan to blue ramps green down, blue to magenta
// ramps red up, magenta to red ramps blue down.
static constexpr Sector kWheel[HUE_CYCLE / 256] = {
    {Green, NoRamp, Red},  {NoRamp, Red, Green}, {Blue, NoRamp, Green},
    {NoRamp, Green, Blue}, {Red, NoRamp, Blue},  {NoRamp, Blue, Red},
};

Rgb oracleHueToRgb(uint16_t hue) {
  const uint16_t turn = static_cast<uint16_t>(hue % HUE_CYCLE);
  const uint8_t ramp = static_cast<uint8_t>(turn & 0xFF);
  const Sector &sector = kWheel[turn >> 8];

  uint8_t channel[3] = {0, 0, 0};
  channel[sector.full] = 255;
  if (sector.up != NoRamp) {
    channel[sector.up] = ramp;
  }
  if (sector.down != NoRamp) {
    channel[sector.down] = static_cast<uint8_t>(255 - ramp);
  }
  return Rgb{channel[Green], channel[Red], channel[Blue]};
}

// The first hue of the wheel that produces this colour, or -1 when the wheel
// has no such colour.
int realizedHue(Rgb colour) {
  for (uint16_t hue = 0; hue < HUE_CYCLE; ++hue) {
    const Rgb wheel = oracleHueToRgb(hue);
    if (wheel.g == colour.g && wheel.r == colour.r && wheel.b == colour.b) {
      return static_cast<int>(hue);
    }
  }
  return -1;
}

// ── Reporting ──

void report(bool ok, const char *fmt, ...) {
  ++g_checks;
  if (!ok) {
    ++g_failed;
  }
  char line[320];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", line);
}

const char *text(Rgb colour) {
  static char ring[4][32];
  static int next = 0;
  char *out = ring[next];
  next = (next + 1) % 4;
  std::snprintf(out, sizeof(ring[0]), "{g:%3u r:%3u b:%3u}", colour.g, colour.r,
                colour.b);
  return out;
}

// ── The array the render writes into ──
// One element past the range the widest keyCount uses, plus a second one to
// read, so a write off the end lands somewhere visible rather than in the
// variable next to it.
static constexpr uint16_t kProbeElements = LED_BUFFER_ELEMENTS + 2;
Rgb g_buf[kProbeElements];

// A second array of the same size, for the cases that compare two renders.
Rgb g_other[kProbeElements];

// A colour the wheel cannot produce: every one of its colours holds one
// channel at 0 and another at 255, so no colour of it is 0xEE on all three.
static constexpr Rgb kSentinel = {0xEE, 0xEE, 0xEE};

bool isSentinel(Rgb colour) {
  return colour.g == kSentinel.g && colour.r == kSentinel.r &&
         colour.b == kSentinel.b;
}

void fillSentinel() {
  for (Rgb &colour : g_buf) {
    colour = kSentinel;
  }
}

void renderInto(uint8_t keyCount, uint32_t periodUs, uint16_t phaseOffset) {
  fillSentinel();
  ledEffectRender(g_buf, keyCount, periodUs, phaseOffset);
}

// One animation cycle as the effect is configured: the frames of the effect
// span this period, and the tick that advances them runs at the task period.
static constexpr uint32_t kEffectPeriodUs = 1000000;
static constexpr uint32_t kTaskPeriodUs = 10000;

// ── 1. the colour frame 0, LED 0 starts at ──

struct StartCase {
  uint16_t phase;
  Rgb want;
  const char *name;
};

// The primaries and two ramps, written out as colours rather than derived, so
// a wheel that arrives at the right colour by the wrong arithmetic still fails
// here. Fields are in the order the struct carries them: green, red, blue.
const StartCase kStarts[] = {
    {0, {0, 255, 0}, "red"},                                   // red
    {64, {64, 255, 0}, "a quarter of the way to yellow"},      // red -> yellow
    {255, {255, 255, 0}, "yellow, the end of the first ramp"}, // yellow
    {256, {255, 255, 0}, "yellow, the start of the second"},   // yellow
    {512, {255, 0, 0}, "green"},                               // green
    {768, {255, 0, 255}, "cyan"},                              // cyan
    {1024, {0, 0, 255}, "blue"},                               // blue
    {1280, {0, 255, 255}, "magenta"},                          // magenta
    {1408, {0, 255, 127}, "half way back to red"},             // magenta -> red
    {1535, {0, 255, 0}, "the last hue of the turn is red"},    // red
};

void groupStartColour() {
  std::printf(
      "\n-- 1. start colour: frame 0, LED 0 is the wheel at phaseOffset --\n");

  const uint8_t keys[] = {1, 8, 32};
  for (const StartCase &c : kStarts) {
    const bool oracleAgrees = oracleHueToRgb(c.phase).g == c.want.g &&
                              oracleHueToRgb(c.phase).r == c.want.r &&
                              oracleHueToRgb(c.phase).b == c.want.b;
    char line[128];
    std::snprintf(line, sizeof(line), "hue %4u (%s) is %s", c.phase, c.name,
                  text(c.want));
    report(oracleAgrees, "%s — the wheel in this file agrees", line);
  }

  for (uint8_t keyCount : keys) {
    for (const StartCase &c : kStarts) {
      renderInto(keyCount, kEffectPeriodUs, c.phase);
      const Rgb got = g_buf[0];
      char line[128];
      std::snprintf(line, sizeof(line), "keyCount %2u hue %4u: want %s got %s",
                    keyCount, c.phase, text(c.want), text(got));
      report(got.g == c.want.g && got.r == c.want.r && got.b == c.want.b, "%s",
             line);
    }
  }

  // Phases no hand-written table covers: the render's first element against
  // the wheel this file carries, for every third hue of the turn.
  for (uint16_t phase = 0; phase < HUE_CYCLE; phase += 3) {
    renderInto(16, kEffectPeriodUs, phase);
    const Rgb want = oracleHueToRgb(phase);
    if (!(g_buf[0].g == want.g && g_buf[0].r == want.r &&
          g_buf[0].b == want.b)) {
      report(false, "keyCount 16 hue %4u: want %s got %s", phase, text(want),
             text(g_buf[0]));
    }
  }
  report(true, "keyCount 16: 512 phases outside the table all match the wheel");
}

// ── 2. the hue step from one LED to the next ──

void groupLedStep() {
  std::printf(
      "\n-- 2. LED step: neighbours sit HUE_CYCLE / keyCount apart --\n");

  // Phases low enough that a whole frame stays inside one turn, so the hue of
  // a frame's last LED is read as itself rather than as a wrapped value.
  const uint16_t phases[] = {0, 32};
  const uint8_t keys[] = {1, 2, 3, 4, 5, 7, 8, 16, 32};

  for (uint8_t keyCount : keys) {
    for (uint16_t phase : phases) {
      if (keyCount == 1) {
        std::printf("  n/a  keyCount  1 phase %3u: one LED to a frame, no "
                    "neighbour to compare with\n",
                    phase);
        continue;
      }

      renderInto(keyCount, kEffectPeriodUs, phase);

      const double want = static_cast<double>(HUE_CYCLE) / keyCount;
      int minStep = 10000;
      int maxStep = -10000;
      bool stepsMatch = true;
      int previous = realizedHue(g_buf[0]);
      if (previous < 0) {
        stepsMatch = false;
      }
      for (uint8_t led = 1; led < keyCount && stepsMatch; ++led) {
        const int hue = realizedHue(g_buf[led]);
        if (hue < 0) {
          stepsMatch = false;
          break;
        }
        const int step = hue - previous;
        if (step < minStep) {
          minStep = step;
        }
        if (step > maxStep) {
          maxStep = step;
        }
        if (static_cast<double>(step) < want - 1.0 ||
            static_cast<double>(step) > want + 1.0) {
          stepsMatch = false;
        }
        previous = hue;
      }

      // The step is stated in whole hue units, so a frame of keyCount LEDs
      // spans (keyCount - 1) of them. Per-pair tolerance alone lets a step
      // that is one unit short or long through, so the span of the whole frame
      // is held against the same figure: the error of one step is multiplied
      // by the number of them. The tolerance is 2 units because a colour at a
      // sector boundary reads back as the lower of the two hues that produce
      // it, which moves each end of the span by up to one.
      const int span =
          stepsMatch ? realizedHue(g_buf[keyCount - 1]) - realizedHue(g_buf[0])
                     : 0;
      const int wantSpan = (keyCount - 1) * (HUE_CYCLE / keyCount);
      const bool spanMatches =
          stepsMatch && span >= wantSpan - 2 && span <= wantSpan + 2;

      report(stepsMatch && spanMatches,
             "keyCount %2u phase %3u: %2u steps of %d..%d (want %.2f +-1), "
             "span %4d (want %4d +-2)",
             keyCount, phase, static_cast<unsigned>(keyCount - 1), minStep,
             maxStep, want, span, wantSpan);
    }
  }
}

// ── 3. the hue step from one frame to the next ──

void groupFrameStep() {
  std::printf(
      "\n-- 3. frame step: neighbours sit HUE_CYCLE / FRAME_COUNT apart --\n");

  // Phases low enough that the whole run of frames stays below the last hue of
  // the turn, so the hue of a later frame's first LED is read as itself rather
  // than as the red it wraps to: hue 1535 and hue 0 are the same colour, and
  // the last frame reaches phase + 1505.
  const uint16_t phases[] = {0, 29};
  const uint8_t keys[] = {1, 8, 32};

  for (uint8_t keyCount : keys) {
    for (uint16_t phase : phases) {
      renderInto(keyCount, kEffectPeriodUs, phase);

      const double want = static_cast<double>(HUE_CYCLE) / FRAME_COUNT;
      int minStep = 10000;
      int maxStep = -10000;
      bool stepsMatch = true;
      int previous = realizedHue(g_buf[0]);
      if (previous < 0) {
        stepsMatch = false;
      }
      for (uint8_t frame = 1; frame < FRAME_COUNT && stepsMatch; ++frame) {
        // LED 0 of the frame, the element the frame's window starts at.
        const int hue = realizedHue(g_buf[frame * keyCount]);
        if (hue < 0) {
          stepsMatch = false;
          break;
        }
        const int step = hue - previous;
        if (step < minStep) {
          minStep = step;
        }
        if (step > maxStep) {
          maxStep = step;
        }
        if (static_cast<double>(step) < want - 1.0 ||
            static_cast<double>(step) > want + 1.0) {
          stepsMatch = false;
        }
        previous = hue;
      }

      // The 49 steps of the run: a render that truncates per frame keeps every
      // pair inside the tolerance and still ends short by the truncation of
      // each frame, which only the span shows.
      const int span = stepsMatch
                           ? realizedHue(g_buf[(FRAME_COUNT - 1) * keyCount]) -
                                 realizedHue(g_buf[0])
                           : 0;
      const double wantSpan = (FRAME_COUNT - 1) * want;
      const bool spanMatches = stepsMatch &&
                               static_cast<double>(span) >= wantSpan - 1.0 &&
                               static_cast<double>(span) <= wantSpan + 1.0;

      report(stepsMatch && spanMatches,
             "keyCount %2u phase %3u: %2u steps of %d..%d (want %.2f +-1), "
             "span %4d (want %.2f +-1)",
             keyCount, phase, static_cast<unsigned>(FRAME_COUNT - 1), minStep,
             maxStep, want, span, wantSpan);
    }
  }
}

// ── 4. a hue that passes the end of the wheel comes back to its start ──

void groupFullTurn() {
  std::printf("\n-- 4. full turn: phaseOffset and phaseOffset + HUE_CYCLE "
              "render the same --\n");

  const uint8_t keys[] = {1, 8, 32};
  const uint16_t phases[] = {0, 777, 1535};

  for (uint8_t keyCount : keys) {
    for (uint16_t phase : phases) {
      const uint16_t wrapped = static_cast<uint16_t>(phase + HUE_CYCLE);
      const uint16_t elements = static_cast<uint16_t>(FRAME_COUNT * keyCount);

      renderInto(keyCount, kEffectPeriodUs, phase);
      std::memcpy(g_other, g_buf, sizeof(Rgb) * elements);

      renderInto(keyCount, kEffectPeriodUs, wrapped);

      std::size_t firstDiffering = elements;
      for (std::size_t i = 0; i < elements; ++i) {
        if (!(g_other[i].g == g_buf[i].g && g_other[i].r == g_buf[i].r &&
              g_other[i].b == g_buf[i].b)) {
          firstDiffering = i;
          break;
        }
      }

      if (firstDiffering == elements) {
        report(true,
               "keyCount %2u phase %4u: all %4u elements of the turn "
               "match phase %4u",
               keyCount, phase, elements, wrapped);
      } else {
        report(false,
               "keyCount %2u phase %4u: element %zu is %s at phase %4u and "
               "%s a turn later",
               keyCount, phase, firstDiffering, text(g_other[firstDiffering]),
               wrapped, text(g_buf[firstDiffering]));
      }
    }
  }
}

// ── 5. keyCount boundaries and what the render leaves alone ──

void groupBoundaries() {
  std::printf(
      "\n-- 5. keyCount boundaries: what is written and what is left --\n");

  const uint8_t keys[] = {0, 1, 32};
  for (uint8_t keyCount : keys) {
    renderInto(keyCount, kEffectPeriodUs, 0);

    const uint16_t used = static_cast<uint16_t>(FRAME_COUNT * keyCount);
    uint16_t written = 0;
    uint16_t leftComputed = 0;
    for (uint16_t i = 0; i < used; ++i) {
      if (!isSentinel(g_buf[i])) {
        ++written;
      }
    }
    for (uint16_t i = used; i < kProbeElements; ++i) {
      if (isSentinel(g_buf[i])) {
        ++leftComputed;
      }
    }

    const uint16_t untouched = static_cast<uint16_t>(kProbeElements - used);
    report(written == used && leftComputed == untouched,
           "keyCount %2u: %4u elements written, %4u elements past them left "
           "as they were (element %u and up)",
           keyCount, written, untouched, used);
  }

  // The widest keyCount at the buffer's own size: 1,600 elements written, the
  // 448 spare elements of the 2,048-element array untouched, as is element
  // 1,600 as the paint layer will hand it a window.
  renderInto(32, kEffectPeriodUs, 0);
  report(!isSentinel(g_buf[1599]), "keyCount 32: element 1599 is written");
  report(isSentinel(g_buf[1600]), "keyCount 32: element 1600 is untouched");
  report(isSentinel(g_buf[1601]), "keyCount 32: element 1601 is untouched");
}

// ── 6. the clock the 100 Hz tick advances ──

struct ClockCase {
  const char *name;
  uint32_t periodUs;
  int ticks;
  uint32_t deltaUs;
  uint8_t wantFrame;
  uint32_t wantCarried;
};

const ClockCase kClockCases[] = {
    {"1 s cycle, one 10 ms tick", 1000000, 1, 10000, 0, 10000},
    {"1 s cycle, two 10 ms ticks show frame 1", 1000000, 2, 10000, 1, 0},
    {"1 s cycle, 100 ticks land back where they started", 1000000, 100, 10000,
     0, 0},
    {"1 s cycle, two 12 ms ticks: the remainders are spent, not dropped",
     1000000, 2, 12000, 1, 4000},
    {"300 ms cycle, three 10 ms ticks", 300000, 3, 10000, 5, 0},
    {"a cycle under FRAME_COUNT never advances", 40, 3, 10000, 0, 0},
};

void groupClock() {
  std::printf("\n-- 6. clock: the frame the tick advances to and what it "
              "carries --\n");

  for (const ClockCase &c : kClockCases) {
    LedEffectClock clock;
    for (int tick = 0; tick < c.ticks; ++tick) {
      ledEffectAdvance(clock, c.deltaUs, c.periodUs);
    }
    report(clock.frame == c.wantFrame && clock.carriedUs == c.wantCarried,
           "%s: frame want %2u got %2u, carriedUs want %6u got %6u", c.name,
           c.wantFrame, clock.frame, c.wantCarried, clock.carriedUs);
  }

  // The interrupt period the task runs at, for the record: the figures above
  // are the ones the configured cycle and tick produce.
  LedEffectClock clock;
  for (int tick = 0; tick < 100; ++tick) {
    ledEffectAdvance(clock, kTaskPeriodUs, kEffectPeriodUs);
  }
  report(clock.frame == 0 && clock.carriedUs == 0,
         "100 ticks of %u us over a %u us cycle: frame %u, carriedUs %u",
         kTaskPeriodUs, kEffectPeriodUs, clock.frame, clock.carriedUs);
}

} // namespace

int main() {
  std::printf("Led::ledEffectRender -- host cases (no board)\n");
  std::printf("  HUE_CYCLE %u, FRAME_COUNT %u, LED_COUNT %u, "
              "LED_BUFFER_ELEMENTS %u (%u B)\n",
              HUE_CYCLE, FRAME_COUNT, LED_COUNT, LED_BUFFER_ELEMENTS,
              static_cast<unsigned>(LED_BUFFER_ELEMENTS * sizeof(Rgb)));

  groupStartColour();
  groupLedStep();
  groupFrameStep();
  groupFullTurn();
  groupBoundaries();
  groupClock();

  std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
