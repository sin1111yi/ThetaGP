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

#include "drivers/gp_emulator/gp_emulator.h"
#include "pb/enums.pb.h"

namespace ThetaGP::Drivers::GPEmulator {


class GPEmulatorManager {
public:
  GPEmulatorManager(GPEmulatorManager const &) = delete;
  void operator=(GPEmulatorManager const &) = delete;

  static GPEmulatorManager &getInstance() {
    static GPEmulatorManager instance;
    return instance;
  }

  [[nodiscard]] GPEmulator *getEmulator() { return emulator; }

  // The mode the configuration asks for, exactly as the schema declares it. The
  // mapping from that value to the driver that answers it is setup()'s: a value
  // no driver answers is the generic HID device, which is what a board that
  // never set the key reports as.
  static Enums::InputMode inputModeFromConfig();
  void setup(Enums::InputMode mode);
  [[nodiscard]] Enums::InputMode getInputMode() { return inputMode; }
  [[nodiscard]] bool isConfigMode() {
    return inputMode == Enums::InputMode::INPUT_MODE_CONFIG;
  }

private:
  GPEmulatorManager() = default;
  GPEmulator *emulator = nullptr;
  Enums::InputMode inputMode = Enums::InputMode::INPUT_MODE_GENERIC;
};

} // namespace ThetaGP::Drivers::GPEmulator
