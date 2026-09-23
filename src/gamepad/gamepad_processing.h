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

#include "gamepad/config/config_store.h"
#include "gamepad/gamepad_state.h"

namespace ThetaGP::Gamepad {

// Memory the transforms keep between ticks, one entry per transform. Clearing
// it forgets everything held before and leaves this tick's input alone.
struct ProcessingMemory {
  DpadState dpad;
};

// Working area of the processing step: the input the keys produced this tick,
// plus the memory above. A transform rewrites the input in place.
struct ProcessingState {
  GamepadRawInput raw;
  ProcessingMemory memory;
};

// Entry point: hands each slice of the raw input to the transform that owns it,
// in a fixed order.
void processState(ProcessingState &state, const Config::ConfigStore &cfg);

// Direction bits: SOCD cleaning and the four-way filter. `cfg` selects which
// transforms run.
uint8_t processDpad(uint8_t dpad, DpadState &dpadState,
                    const Config::ConfigStore &cfg);

} // namespace ThetaGP::Gamepad
