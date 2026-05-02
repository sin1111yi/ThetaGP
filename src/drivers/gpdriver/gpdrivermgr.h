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

#include "drivers/gpdriver/gpdriver.h"

namespace ThetaGP::Drivers::GPDriver {

enum class InputMode : uint8_t { None = 0, Config, HID, Count };

class GPDriverManager {
public:
  GPDriverManager(GPDriverManager const &) = delete;
  void operator=(GPDriverManager const &) = delete;

  static GPDriverManager &getInstance() {
    static GPDriverManager instance;
    return instance;
  }

  GPDriver *getgpdriverDevice() { return usbdevice; }
  void setup(InputMode mode);
  InputMode getInputMode() { return inputMode; }
  bool isConfigMode() { return (inputMode == InputMode::Config); }

private:
  GPDriverManager() = default;
  GPDriver *usbdevice = nullptr;
  InputMode inputMode = InputMode::None;
};

} // namespace ThetaGP::Drivers::GPDriver
