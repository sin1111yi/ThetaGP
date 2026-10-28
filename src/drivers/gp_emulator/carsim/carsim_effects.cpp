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

#include "drivers/gp_emulator/carsim/carsim_effects.h"

#include <cstring>

namespace ThetaGP::Drivers::GPEmulator {
namespace {

// The middle of the steering range, where the wheel rests and where a spring
// effect pulls it back to.
constexpr int32_t kSteerCenter = 32768;

// The full torque, in the units a HID effect carries.
constexpr int32_t kEffectFullScale = 32767;

// A period that would divide by nothing plays as a held offset.
constexpr uint16_t kShortestPeriodMs = 1;

// One waveform of a periodic effect at `elapsedMs` into a period of `periodMs`,
// scaled to the effect's full range.
int32_t waveform(uint8_t type, uint32_t elapsedMs, uint16_t periodMs) {
  const uint32_t period = periodMs == 0 ? kShortestPeriodMs : periodMs;
  const uint32_t at = elapsedMs % period;

  switch (type) {
  case CARSIM_EFFECT_SQUARE:
    return at < period / 2 ? kEffectFullScale : -kEffectFullScale;
  case CARSIM_EFFECT_SINE: {
    // A quarter of the period is a quarter turn of a triangle approximation of a
    // sine, which is within a few percent over the whole period.
    const int32_t quarter = static_cast<int32_t>(period / 4);
    const int32_t at32 = static_cast<int32_t>(at);
    int32_t triangle;
    if (at < period / 4) {
      triangle = at32 * kEffectFullScale / quarter;
    } else if (at < period * 3 / 4) {
      triangle = kEffectFullScale -
                 (at32 - quarter) * 2 * kEffectFullScale / (2 * quarter);
    } else {
      triangle = -kEffectFullScale +
                 (at32 - period * 3 / 4) * kEffectFullScale / quarter;
    }
    return triangle;
  }
  case CARSIM_EFFECT_TRIANGLE: {
    const int32_t half = static_cast<int32_t>(period / 2);
    const int32_t at32 = static_cast<int32_t>(at);
    if (at < period / 2) {
      return -kEffectFullScale + at32 * 2 * kEffectFullScale / half;
    }
    return kEffectFullScale - (at32 - half) * 2 * kEffectFullScale / half;
  }
  case CARSIM_EFFECT_SAW_UP:
    return -kEffectFullScale + static_cast<int32_t>(at) * 2 * kEffectFullScale /
                                   static_cast<int32_t>(period);
  case CARSIM_EFFECT_SAW_DOWN:
    return kEffectFullScale - static_cast<int32_t>(at) * 2 * kEffectFullScale /
                                  static_cast<int32_t>(period);
  default:
    return 0;
  }
}

// Scales a force toward zero by the envelope the effect carries at `elapsedMs`.
int32_t envelope(uint32_t elapsedMs, uint16_t durationMs,
                 const CarSimEffect &effect) {
  int32_t scale = kEffectFullScale;
  if (effect.attackTime != 0 && elapsedMs < effect.attackTime) {
    scale = static_cast<int32_t>(effect.attackLevel) +
            (kEffectFullScale - static_cast<int32_t>(effect.attackLevel)) *
                static_cast<int32_t>(elapsedMs) / effect.attackTime;
  }
  if (effect.fadeTime != 0 && durationMs != CarSimEffects::kInfiniteMs &&
      durationMs > effect.fadeTime && elapsedMs + effect.fadeTime > durationMs) {
    const uint32_t into = elapsedMs + effect.fadeTime - durationMs;
    scale = scale * (kEffectFullScale - static_cast<int32_t>(effect.fadeLevel) *
                                           static_cast<int32_t>(into) /
                                           effect.fadeTime) /
            kEffectFullScale;
  }
  return scale;
}

// Clamps a force to the saturation the condition names.
int32_t saturate(int32_t force, uint16_t positive, uint16_t negative) {
  if (force > 0 && static_cast<uint32_t>(force) > positive) {
    return positive;
  }
  if (force < 0 && static_cast<uint32_t>(-force) > negative) {
    return -static_cast<int32_t>(negative);
  }
  return force;
}

} // namespace

void CarSimEffects::reset() {
  for (CarSimEffect &block : _blocks) {
    block = CarSimEffect{};
  }
  _enabled = false;
  _gain = kDefaultGain;
  _pending = 0;
}

CarSimEffect *CarSimEffects::block(uint8_t index) {
  if (index >= CARSIM_EFFECT_BLOCKS) {
    return nullptr;
  }
  return &_blocks[index];
}

bool CarSimEffects::handle(uint8_t reportId, const uint8_t *data,
                           uint16_t length) {
  switch (reportId) {
  case CARSIM_REPORT_CREATE_EFFECT: {
    // The host asks for a block of a type; the pool hands out the first free one
    // and reports which one it took when the host reads the block load back.
    const uint8_t type = length > 1 ? data[1] : 0;
    for (uint8_t i = 0; i < CARSIM_EFFECT_BLOCKS; ++i) {
      if (_blocks[i].type == CARSIM_EFFECT_NONE) {
        _blocks[i] = CarSimEffect{};
        _blocks[i].type = type;
        _blocks[i].gain = kDefaultGain;
        _pending = i;
        return true;
      }
    }
    _pending = CARSIM_EFFECT_BLOCKS;
    return false;
  }

  case CARSIM_REPORT_SET_EFFECT: {
    CarSimSetEffectReport report{};
    std::memcpy(&report, data, sizeof report);
    CarSimEffect *slot = block(report.block);
    if (slot == nullptr) {
      return false;
    }
    slot->type = report.type;
    slot->durationMs = report.duration;
    slot->gain = report.gain;
    slot->direction = report.direction;
    return true;
  }

  case CARSIM_REPORT_SET_CONSTANT: {
    CarSimSetConstantReport report{};
    std::memcpy(&report, data, sizeof report);
    CarSimEffect *slot = block(report.block);
    if (slot == nullptr) {
      return false;
    }
    // A constant force is a ramp whose ends meet.
    slot->magnitude = report.magnitude;
    slot->rampStart = report.magnitude;
    slot->rampEnd = report.magnitude;
    return true;
  }

  case CARSIM_REPORT_SET_PERIODIC: {
    CarSimSetPeriodicReport report{};
    std::memcpy(&report, data, sizeof report);
    CarSimEffect *slot = block(report.block);
    if (slot == nullptr) {
      return false;
    }
    slot->magnitude = report.magnitude;
    slot->offset = report.offset;
    slot->phase = report.phase;
    slot->periodMs = report.period;
    return true;
  }

  case CARSIM_REPORT_SET_CONDITION: {
    CarSimSetConditionReport report{};
    std::memcpy(&report, data, sizeof report);
    CarSimEffect *slot = block(report.block);
    if (slot == nullptr) {
      return false;
    }
    // The wheel has one axis, so only the first parameter block is read.
    if (report.parameterBlock == 1) {
      slot->cpOffset = report.cpOffset;
      slot->positiveCoefficient = report.positiveCoefficient;
      slot->negativeCoefficient = report.negativeCoefficient;
      slot->positiveSaturation = report.positiveSaturation;
      slot->negativeSaturation = report.negativeSaturation;
      slot->deadBand = report.deadBand;
    }
    return true;
  }

  case CARSIM_REPORT_SET_ENVELOPE: {
    CarSimSetEnvelopeReport report{};
    std::memcpy(&report, data, sizeof report);
    CarSimEffect *slot = block(report.block);
    if (slot == nullptr) {
      return false;
    }
    slot->attackLevel = report.attackLevel;
    slot->attackTime = report.attackTime;
    slot->fadeLevel = report.fadeLevel;
    slot->fadeTime = report.fadeTime;
    return true;
  }

  case CARSIM_REPORT_EFFECT_OPERATION: {
    CarSimEffectOperationReport report{};
    std::memcpy(&report, data, sizeof report);
    CarSimEffect *slot = block(report.block);
    if (slot == nullptr) {
      return false;
    }
    if (report.operation == CARSIM_OPERATION_START) {
      slot->playing = true;
      slot->startedMs = 0;
      slot->playedMs = 0;
    } else if (report.operation == CARSIM_OPERATION_STOP) {
      slot->playing = false;
    }
    return true;
  }

  case CARSIM_REPORT_BLOCK_FREE: {
    CarSimEffect *slot = block(length != 0 ? data[0] : 0);
    if (slot == nullptr) {
      return false;
    }
    *slot = CarSimEffect{};
    return true;
  }

  case CARSIM_REPORT_DEVICE_CONTROL: {
    const uint8_t control = length != 0 ? data[0] : 0;
    switch (control) {
    case CARSIM_CONTROL_ENABLE:
      _enabled = true;
      return true;
    case CARSIM_CONTROL_DISABLE:
    case CARSIM_CONTROL_RESET:
      _enabled = false;
      return true;
    case CARSIM_CONTROL_STOP_ALL:
      for (CarSimEffect &slot : _blocks) {
        slot.playing = false;
      }
      return true;
    default:
      return true;
    }
  }

  case CARSIM_REPORT_DEVICE_GAIN:
    _gain = length != 0 ? data[0] : kDefaultGain;
    return true;

  default:
    return false;
  }
}

uint16_t CarSimEffects::answer(uint8_t reportId, uint8_t *out,
                               uint16_t capacity) const {
  switch (reportId) {
  case CARSIM_REPORT_POOL: {
    CarSimPoolReport report{};
    report.poolSize = CARSIM_POOL_SIZE;
    report.simultaneousMax = CARSIM_EFFECT_BLOCKS;
    report.deviceManaged = 1;
    const uint16_t size = sizeof report < capacity ? sizeof report : capacity;
    std::memcpy(out, &report, size);
    return size;
  }

  case CARSIM_REPORT_BLOCK_LOAD: {
    // The pool takes an effect whenever a block is free, and the report says how
    // much room is left for the host to hold against what it asked for.
    CarSimBlockLoadReport report{};
    uint8_t used = 0;
    for (const CarSimEffect &slot : _blocks) {
      if (slot.type != CARSIM_EFFECT_NONE) {
        ++used;
      }
    }
    report.block = _pending;
    report.poolAvailable =
        static_cast<uint16_t>(CARSIM_POOL_SIZE -
                              used * (CARSIM_POOL_SIZE / CARSIM_EFFECT_BLOCKS));
    report.status = _pending < CARSIM_EFFECT_BLOCKS ? CARSIM_BLOCK_LOAD_SUCCESS
                                                    : CARSIM_BLOCK_LOAD_FULL;
    const uint16_t size = sizeof report < capacity ? sizeof report : capacity;
    std::memcpy(out, &report, size);
    return size;
  }

  default:
    return 0;
  }
}

int16_t CarSimEffects::torque(uint16_t angle, uint16_t previousAngle,
                              uint32_t deltaUs, uint32_t nowMs) {
  if (!_enabled) {
    return 0;
  }

  const int32_t position = static_cast<int32_t>(angle) - kSteerCenter;
  const int32_t moved =
      static_cast<int32_t>(angle) - static_cast<int32_t>(previousAngle);
  const int64_t velocity =
      deltaUs == 0 ? 0 : (static_cast<int64_t>(moved) * 1000000) / deltaUs;

  int32_t sum = 0;
  for (CarSimEffect &slot : _blocks) {
    if (!slot.playing || slot.type == CARSIM_EFFECT_NONE) {
      continue;
    }
    if (slot.startedMs == 0) {
      slot.startedMs = nowMs;
    }
    slot.playedMs = nowMs - slot.startedMs;
    if (slot.durationMs != kInfiniteMs && slot.playedMs >= slot.durationMs) {
      slot.playing = false;
      continue;
    }

    int32_t force = 0;
    switch (slot.type) {
    case CARSIM_EFFECT_CONSTANT:
      force = slot.magnitude;
      break;

    case CARSIM_EFFECT_RAMP: {
      const int32_t span =
          slot.durationMs == kInfiniteMs ? 0 : static_cast<int32_t>(slot.durationMs);
      const int32_t at = span == 0 ? 0 : static_cast<int32_t>(slot.playedMs);
      force = slot.rampStart +
              (span == 0 ? 0 : (slot.rampEnd - slot.rampStart) * at / span);
      break;
    }

    case CARSIM_EFFECT_SQUARE:
    case CARSIM_EFFECT_SINE:
    case CARSIM_EFFECT_TRIANGLE:
    case CARSIM_EFFECT_SAW_UP:
    case CARSIM_EFFECT_SAW_DOWN: {
      const int32_t wave = waveform(slot.type, slot.playedMs, slot.periodMs);
      force = slot.offset + wave * slot.magnitude / kEffectFullScale;
      break;
    }

    case CARSIM_EFFECT_SPRING: {
      const int32_t displacement = position - slot.cpOffset;
      if (displacement > static_cast<int32_t>(slot.deadBand) ||
          displacement < -static_cast<int32_t>(slot.deadBand)) {
        const int32_t coefficient = displacement > 0 ? slot.positiveCoefficient
                                                     : slot.negativeCoefficient;
        force = coefficient * displacement / kEffectFullScale;
        force = saturate(force, slot.positiveSaturation,
                         slot.negativeSaturation);
      }
      break;
    }

    case CARSIM_EFFECT_DAMPER: {
      const int32_t coefficient =
          velocity > 0 ? slot.positiveCoefficient : slot.negativeCoefficient;
      force = static_cast<int32_t>(coefficient * velocity /
                                   (kSteerCenter * 4));
      force = saturate(force, slot.positiveSaturation,
                       slot.negativeSaturation);
      break;
    }

    case CARSIM_EFFECT_INERTIA: {
      // The acceleration is the velocity this tick carries, which is what the
      // angle moved against the last one; a wheel that moved further pushes back
      // harder.
      const int32_t coefficient = moved > 0 ? slot.positiveCoefficient
                                            : slot.negativeCoefficient;
      force = coefficient * moved / 64;
      force = saturate(force, slot.positiveSaturation,
                       slot.negativeSaturation);
      break;
    }

    case CARSIM_EFFECT_FRICTION: {
      if (velocity != 0) {
        const int32_t coefficient =
            velocity > 0 ? slot.negativeCoefficient : slot.positiveCoefficient;
        force = saturate(coefficient, slot.positiveSaturation,
                         slot.negativeSaturation);
      }
      break;
    }

    default:
      break;
    }

    force = force * slot.gain / kDefaultGain;
    force = force * envelope(slot.playedMs, slot.durationMs, slot) /
            kEffectFullScale;
    sum += force;
  }

  sum = sum * _gain / kDefaultGain;
  sum = sum * 1000 / kEffectFullScale;
  if (sum > 1000) {
    sum = 1000;
  }
  if (sum < -1000) {
    sum = -1000;
  }
  return static_cast<int16_t>(sum);
}

} // namespace ThetaGP::Drivers::GPEmulator
