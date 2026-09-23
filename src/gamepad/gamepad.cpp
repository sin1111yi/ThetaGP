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

#include "gamepad/gamepad.h"

#include "build_info.h"
#include "gamepad/config/configmgr.h"
#include "gamepad/input/input_processor.h"
#include "utils/log/log.h"

#include "drivers/device/keypad.h"
#include "drivers/gpdriver/gpdrivermgr.h"
#include <cstdint>
#include <cstring>

namespace ThetaGP::Gamepad {

using Keypad = ThetaGP::Drivers::Device::Keypad;

Gamepad::Gamepad() { setup(); }

void Gamepad::setup() {
  _state = GamepadRawInput{};
  _inputDevice = nullptr;
  _initialized = true;
  _ready = false;

  _cfg = &Config::ConfigManager::getInstance().config();
  _gpDriverMgr = &Drivers::GPDriver::GPDriverManager::getInstance();
}

void Gamepad::reinit() {
  Input::InputProcessor::getInstance().reset();
  setup();
}

/**
 * @brief Register a keypad device
 * @param device Reference to Device (must be Keypad)
 */
void Gamepad::registerKeypadDevice(Device *device) {
  if (std::strcmp(device->getName(), "keypad") == 0) {
    _inputDevice = device;
    _ready = true;
  }
}

/**
 * @brief Read input from registered keypad device
 */
void Gamepad::read() {
  if (!_inputDevice || !_inputDevice->isInitialized()) {
    return;
  }

  Keypad &keypad = static_cast<Keypad &>(*_inputDevice);

  _state.buttons = 0;
  _state.dpad = 0;

  uint32_t keypadMask = keypad.getPressed();

  // Process all 32 physical keys
  for (uint8_t i = 0; i < 32; i++) {
    if (keypadMask & (1U << i)) {
      uint8_t buttonIndex = _cfg->btn_map[i];
      if (buttonIndex != 0xFF) {
        _state.buttons |= (1U << buttonIndex);

        // Handle D-pad (buttons 0-3) - set dpad bits directly
        if (buttonIndex < 4) {
          _state.dpad |= (1U << buttonIndex);
        }
      }
    }
  }
}

/**
 * @brief Main entry point for task scheduler
 */
void Gamepad::process() {
  if (!_initialized || !_ready) {
    return;
  }
  read();
  Input::InputProcessor::getInstance().process(*_cfg, _state);

  _gpDriverMgr->getgpdriverDevice()->process(this);
}

} // namespace ThetaGP::Gamepad
