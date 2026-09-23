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
#include "gamepad/gamepadstate.h"

namespace ThetaGP::Gamepad::Input {

/**
 * @brief Turns the key state read from the keypad into the state the output
 * drivers report.
 *
 * The step between `Gamepad::read()` and the driver call: SOCD cleaning and
 * the four-way filter, both configured by the active configuration. The
 * state the filters keep between ticks lives in this instance.
 */
class InputProcessor {
public:
  static InputProcessor &getInstance();

  /**
   * @brief Map the raw key state onto the report state, in place.
   *
   * `state.dpadOriginal` receives the dpad value as read, `state.dpad` the
   * value after the filters. `state.buttons` is left as `read()` produced it.
   */
  void process(const Config::ConfigStore &cfg, GamepadRawInput &state);

  // Drop the press order and the last direction each axis saw.
  void reset();

private:
  InputProcessor() = default;

  DpadState _dpad;
};

} // namespace ThetaGP::Gamepad::Input
