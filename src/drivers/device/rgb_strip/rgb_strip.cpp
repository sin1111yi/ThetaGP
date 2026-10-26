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

#include "rgb_strip.h"

#include "drivers/device/rgb_strip/led_effect.h"
#include "drivers/device/system_timer.h"

#include "build_info.h"
#include "conf/ThetaGP_Config.h"
#include "configs/config_keys.gen.h"
#include "gamepad/config/config_manager.h"
#include "pb/enums.pb.h"

#include "utils/log/log.h"

// A board without a strip declares no such table.
#if THETAGP_CFG_HAS_RGB_STRIP

using namespace ThetaGP::Drivers::Peripheral;
using namespace ThetaGP::Drivers::Peripheral::GPIO;
using namespace ThetaGP::Drivers::Peripheral::TIMER;

namespace ThetaGP::Drivers::Device {

namespace {

// One LED per pixel of the strip the board declares.
constexpr uint8_t LED_COUNT = BDCFG_LED_RGB_STRIP_NUMBER;

// WS2812B bit timing: a 1.25 us carrier period, high for 0.4 us for a zero and
// 0.8 us for a one. The strip latches a frame after the line has been low for
// 280 us, which the zero-duty slots after the last pixel cover.
constexpr uint32_t kBitRateHz = 800000;
constexpr uint32_t kBitPeriodNs = 1250;
constexpr uint32_t kZeroHighNs = 400;
constexpr uint32_t kOneHighNs = 800;
constexpr uint32_t kResetNs = 300000;

constexpr uint8_t kBitsPerPixel = 24;
constexpr uint16_t kResetSlots =
    static_cast<uint16_t>((kResetNs + kBitPeriodNs - 1) / kBitPeriodNs);
constexpr uint16_t kSlotCount =
    static_cast<uint16_t>((LED_COUNT + THETAGP_CFG_LED_TRAILING_PIXELS) *
                          kBitsPerPixel) + kResetSlots;

// The rate a frame reaches the strip at, in Hz: the value of the config key
// that carries it, gone back to the declaration's default for a store that
// never carried one.
uint32_t refreshRateHz() {
  const uint8_t hz = Gamepad::Config::ConfigManager::getInstance().config().led.rgb.hz;
  return hz > 0 ? hz : static_cast<uint32_t>(Gamepad::Config::kKeyDefaultLedRgbHz);
}

// The interval one frame is shown for, in microseconds: what a tick of the task
// is spent on before the next frame is due.
uint32_t frameIntervalUs() { return 1000000UL / refreshRateHz(); }

// The most the board's electrical limit allows, in 1/256ths, so the scaling
// below stays integer.
constexpr uint32_t kBrightnessQ8 =
    static_cast<uint32_t>(THETAGP_CFG_LED_BRIGHTNESS_LIMIT * 256.0f);

// The brightness key's ceiling is the whole of that limit: a profile's setting
// is a share of it, and the ceiling is held to the key's own declaration.
constexpr uint8_t kFullBrightness = 128;
static_assert(kFullBrightness ==
                  Gamepad::Config::keyEntry(Gamepad::Config::ConfigKey::LedBri).maxVal,
              "the brightness the board allows is the key's ceiling: raise the "
              "row in configs/config_keys.toml with this constant");

// The share of the limit the profile asks for, in 1/256ths. A brightness the
// key does not reach leaves the strips at the limit rather than above it.
uint32_t brightnessQ8() {
  const uint8_t asked =
      Gamepad::Config::ConfigManager::getInstance().config().led.bri;
  const uint32_t share = asked < kFullBrightness ? asked : kFullBrightness;
  return kBrightnessQ8 * share / kFullBrightness;
}

uint8_t limited(uint8_t value, uint32_t scaleQ8) {
  const uint32_t scaled = (value * scaleQ8) >> 8;
  return static_cast<uint8_t>(scaled > 255U ? 255U : scaled);
}

// One duty per bit slot, zero-filled. AXI SRAM (COMMON_ZERO_INIT): the DMA
// reads it.
COMMON_ZERO_INIT uint16_t s_slots[kSlotCount];

// One animation cycle, rendered once at boot. AXI SRAM: a large, cold array.
COMMON_ZERO_INIT LedEffect::Rgb s_frames[LedEffect::FRAME_COUNT * LED_COUNT];

// The frame the animation is on, and the one the strip was last handed. The
// second starts outside the range of the first, so the first tick sends.
LedEffect::Clock s_clock;
uint8_t s_sentFrame = LedEffect::FRAME_COUNT;
uint32_t s_lastTickUs = 0;

// The look the frames were built for. It starts outside every value a key
// holds, so the first tick builds them.
uint8_t s_shownMode = 0xFF;
uint16_t s_shownHue = 0xFFFF;
uint8_t s_shownSat = 0xFF;


// The colour a still strip holds: the hue a profile carries, moved towards
// white as little as its saturation asks.
LedEffect::Rgb stillColour(uint16_t degrees, uint8_t saturation) {
  return LedEffect::desaturate(
      LedEffect::hueToRgb(LedEffect::unitsOfDegrees(degrees)), saturation);
}

// A frame has to stay up for at least one tick of the task that advances the
// frames: the shortest one is the fastest rate the config key accepts.
static_assert(1000000 / Gamepad::Config::keyEntry(Gamepad::Config::ConfigKey::LedRgbHz).maxVal >=
                  THETAGP_CFG_LED_TASK_PERIOD_US,
              "the fastest LED refresh rate has to cover at least one tick of "
              "the task that advances the frames: lower the key's ceiling or "
              "shorten THETAGP_CFG_LED_TASK_PERIOD_US");

} // namespace

void RgbStrip::init() {
  if (!_timer.initPwm(BDCFG_LED_RGB_STRIP_SOURCE,
                      PinDesc BDCFG_LED_RGB_STRIP_PIN, kBitRateHz)) {
    LOG_ERROR("rgb strip: the timer channel did not come up");
    return;
  }

  // A bit's high time as a duty on the carrier the timer made.
  const uint32_t period = _timer.periodTicks();
  _zeroDuty = static_cast<uint16_t>(period * kZeroHighNs / kBitPeriodNs);
  _oneDuty = static_cast<uint16_t>(period * kOneHighNs / kBitPeriodNs);

  // The first tick builds the frames for the effect the profile asks for.
  s_lastTickUs = SystemTimer::getInstance().getMillis() * 1000U;
  _initialized = true;

  // The pixels this drives have to be the ones wired: a strip longer than the
  // count is driven leaves its far end holding what it latched before.
  LOG_INFO("rgb strip: %u pixels at %lu Hz refresh",
           static_cast<uint32_t>(LED_COUNT),
           static_cast<uint32_t>(refreshRateHz()));
}

void RgbStrip::task(uint32_t currentTimeUs) {
  RgbStrip &strip = getInstance();
  if (!strip.isInitialized()) {
    return;
  }

  const uint32_t deltaUs = currentTimeUs - s_lastTickUs;
  s_lastTickUs = currentTimeUs;

  const Gamepad::Config::Led &led =
      Gamepad::Config::ConfigManager::getInstance().config().led;
  const bool rainbow = led.mode == static_cast<uint8_t>(Enums::LEDMode::Rainbow);
  const bool solid = led.mode == static_cast<uint8_t>(Enums::LEDMode::Solid);

  // A look a profile has moved to is rendered before the frame is chosen: the
  // mode, the hue and the saturation are what the frames are built from.
  if (led.mode != s_shownMode || led.hue != s_shownHue || led.sat != s_shownSat) {
    s_shownMode = led.mode;
    s_shownHue = led.hue;
    s_shownSat = led.sat;

    if (rainbow) {
      // The wheel starts at the hue the profile names, at the saturation it
      // names: the same two numbers the still effect is drawn from.
      LedEffect::render(s_frames, LED_COUNT,
                        LedEffect::unitsOfDegrees(led.hue), led.sat);
    } else {
      const LedEffect::Rgb colour = solid ? stillColour(led.hue, led.sat) : LedEffect::Rgb{};
      for (uint8_t i = 0; i < LED_COUNT; ++i) {
        s_frames[i] = colour;
      }
    }
    // The look changed, so no frame of the new one has been sent yet.
    s_sentFrame = LedEffect::FRAME_COUNT;
  }

  if (rainbow) {
    LedEffect::advance(s_clock, deltaUs, frameIntervalUs());
  } else {
    // A still strip is not advanced: it shows the one frame and nothing after
    // it, so the clock is held on that frame.
    s_clock.frame = 0;
  }

  // A tick that lands on the frame already on the line has nothing to send; a
  // sequence the strip is still reading leaves the frame for the tick after.
  if (s_clock.frame == s_sentFrame) {
    return;
  }

  if (strip.send(&s_frames[s_clock.frame * LED_COUNT])) {
    s_sentFrame = s_clock.frame;
  }
}

bool RgbStrip::send(const LedEffect::Rgb *frame) {
  encode(frame);
  return _timer.startSequence(s_slots, kSlotCount);
}

void RgbStrip::encode(const LedEffect::Rgb *frame) {
  // A pixel is three bytes in the order the wire carries them — green, red,
  // blue, most significant bit first — each taken down to the configured
  // share of full scale.
  const uint8_t *pixels = reinterpret_cast<const uint8_t *>(frame);
  const uint32_t scaleQ8 = brightnessQ8();
  uint16_t slot = 0;

  // The chips wired past the ones this board lights are handed a black pixel
  // each: a chip a frame never reaches holds whatever it latched, and the chip
  // at the end of the chain wants clocks after its own data to latch on.
  const uint16_t trailing = THETAGP_CFG_LED_TRAILING_PIXELS * kBitsPerPixel;

  for (uint8_t pixel = 0; pixel < LED_COUNT; ++pixel) {
    const uint8_t *channels = &pixels[pixel * 3];
    uint8_t share[3];
    for (uint8_t channel = 0; channel < 3; ++channel) {
      share[channel] = limited(channels[channel], scaleQ8);
    }

    for (uint8_t bit = 0; bit < kBitsPerPixel; ++bit) {
      const uint8_t set = (share[bit / 8] >> (7 - (bit % 8))) & 1;
      s_slots[slot++] = set != 0 ? _oneDuty : _zeroDuty;
    }
  }

  for (uint16_t i = 0; i < trailing; ++i) {
    s_slots[slot++] = _zeroDuty;
  }

  for (uint16_t i = 0; i < kResetSlots; ++i) {
    s_slots[slot++] = 0;
  }
}

} // namespace ThetaGP::Drivers::Device

#endif // THETAGP_CFG_HAS_RGB_STRIP
