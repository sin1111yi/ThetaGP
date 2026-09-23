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

#include "gamepad/gamepad_processing.h"

namespace ThetaGP::Gamepad {

void processState(GamepadRawInput &state, DpadState &dpadState,
                  const Config::ConfigStore &cfg) {
  // The value the keys produced, before the transforms below touch it.
  state.dpadOriginal = state.dpad;

  uint8_t dpad = runSOCDCleaner(dpadState,
                                static_cast<Enums::SOCDMode>(cfg.socd_mode),
                                state.dpad);
  if (cfg.four_way_mode != 0) {
    dpad = filterToFourWayMode(dpadState, dpad);
  }

  state.dpad = dpad;
}

} // namespace ThetaGP::Gamepad
