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

// Entry point: hands each slice of the raw state to the transform that owns
// it, in a fixed order.
void processState(GamepadRawInput &state, DpadState &dpadState,
                  const Config::ConfigStore &cfg);

// Direction bits: SOCD cleaning and the four-way filter. `dpadState` carries
// the memory the transforms keep between ticks, `cfg` selects which run.
uint8_t processDpad(uint8_t dpad, DpadState &dpadState,
                    const Config::ConfigStore &cfg);

} // namespace ThetaGP::Gamepad
