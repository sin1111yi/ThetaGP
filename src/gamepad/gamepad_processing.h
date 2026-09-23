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

// Turns the state the keys produced into the state the output drivers report.
// `dpadState` carries the memory the transforms keep between ticks, `cfg`
// selects which transforms run.
void processState(GamepadRawInput &state, DpadState &dpadState,
                  const Config::ConfigStore &cfg);

} // namespace ThetaGP::Gamepad
