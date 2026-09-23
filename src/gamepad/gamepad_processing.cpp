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

void processState(ProcessingState &state, const Config::ConfigStore &cfg) {
  state.raw.dpad = processDpad(state.raw.dpad, state.memory.dpad, cfg);
}

uint8_t processDpad(uint8_t dpad, DpadState &dpadState,
                    const Config::ConfigStore &cfg) {
  uint8_t processed = runSOCDCleaner(
      dpadState, static_cast<Enums::SOCDMode>(cfg.socd_mode), dpad);
  if (cfg.four_way_mode != 0) {
    processed = filterToFourWayMode(dpadState, processed);
  }

  return processed;
}

} // namespace ThetaGP::Gamepad
