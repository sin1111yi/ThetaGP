#include "drivers/device/rgb_strip/led_effect.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

using namespace ThetaGP::Drivers::Device::LedEffect;

int main() {
  unsigned checks = 0;
  for (uint8_t count : {uint8_t{1}, uint8_t{8}, uint8_t{32}}) {
    for (uint16_t offset : {uint16_t{0}, uint16_t{511}, uint16_t{1535}}) {
      for (uint8_t saturation : {uint8_t{0}, uint8_t{127}, uint8_t{255}}) {
        Rgb frame[32]{};
        for (uint8_t f = 0; f < FRAME_COUNT; ++f) {
          renderFrame(frame, count, f, offset, saturation);
          for (uint8_t led = 0; led < count; ++led) {
            const uint16_t hue = static_cast<uint16_t>(
                offset + led * (HUE_CYCLE / count) + f * HUE_CYCLE / FRAME_COUNT);
            const Rgb expected = desaturate(hueToRgb(hue), saturation);
            assert(frame[led].r == expected.r && frame[led].g == expected.g &&
                   frame[led].b == expected.b);
            ++checks;
          }
        }
      }
    }
  }
  Clock clock{};
  clock.carriedUs = 999;
  clock.frame = 49;
  advance(clock, UINT32_MAX, 1000);
  const uint64_t elapsed = uint64_t{999} + UINT32_MAX;
  assert(clock.carriedUs == elapsed % 1000);
  assert(clock.frame == (49 + elapsed / 1000) % FRAME_COUNT);
  ++checks;
  std::printf("%u checks, 0 failed\n", checks);
}
