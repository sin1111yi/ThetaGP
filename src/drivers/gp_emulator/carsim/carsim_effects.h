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

#include "drivers/gp_emulator/carsim/carsim_descriptors.h"

#include <cstdint>

namespace ThetaGP::Drivers::GPEmulator {

// One block of the effect pool: what the host uploaded into it, and whether it is
// playing. The fields are the ones the reports above write; a field a type does
// not use keeps whatever the host last wrote into it.
struct CarSimEffect {
  uint8_t type = CARSIM_EFFECT_NONE;

  // How long the effect plays, in milliseconds; 0x7FFF plays until stopped.
  uint16_t durationMs = 0;
  uint8_t gain = 0xFF;
  uint16_t direction = 0;

  int16_t magnitude = 0;
  int16_t rampStart = 0;
  int16_t rampEnd = 0;
  int16_t offset = 0;
  uint16_t phase = 0;
  uint16_t periodMs = 0;

  int16_t cpOffset = 0;
  int16_t positiveCoefficient = 0;
  int16_t negativeCoefficient = 0;
  uint16_t positiveSaturation = 0;
  uint16_t negativeSaturation = 0;
  uint16_t deadBand = 0;

  uint16_t attackLevel = 0;
  uint16_t attackTime = 0;
  uint16_t fadeLevel = 0;
  uint16_t fadeTime = 0;

  bool playing = false;
  uint32_t startedMs = 0;
  uint32_t playedMs = 0;
};

// The pool of effects the host uploads into, and the torque they add up to. The
// controller has one of these: reports arrive from the host on the wire, and the
// wheel asks it for a torque once per tick.
class CarSimEffects {
public:
  // Where a block's play time has reached: a playing effect stops when its
  // duration runs out, and a block that is not playing adds nothing.
  static constexpr uint16_t kInfiniteMs = 0x7FFF;

  // The default gain the host is told about until it writes one.
  static constexpr uint8_t kDefaultGain = 0xFF;

  // Drops every effect and leaves the pool empty.
  void reset();

  // Takes one output report; answers whether it was a force feedback one.
  bool handle(uint8_t reportId, const uint8_t *data, uint16_t length);

  // Writes a feature report the host asked for, and answers how many bytes it
  // wrote; 0 when the id names no report this pool carries.
  uint16_t answer(uint8_t reportId, uint8_t *out, uint16_t capacity) const;

  // The torque the effects add up to, in thousandths of the rated torque, from
  // the wheel's angle and how far it moved since the previous tick.
  int16_t torque(uint16_t angle, uint16_t previousAngle, uint32_t deltaUs,
                 uint32_t nowMs);

  [[nodiscard]] bool enabled() const { return _enabled; }

private:
  // The block index a report names, or nullptr when it names none.
  CarSimEffect *block(uint8_t index);

  CarSimEffect _blocks[CARSIM_EFFECT_BLOCKS];
  bool _enabled = false;
  uint8_t _gain = kDefaultGain;
  // The block the last create took, which the host is told when it reads the
  // block load back. CARSIM_EFFECT_BLOCKS stands for none.
  uint8_t _pending = 0;
};

} // namespace ThetaGP::Drivers::GPEmulator
