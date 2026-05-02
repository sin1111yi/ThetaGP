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
#include "drivers/device/keypad.h"

namespace ThetaGP::Gamepad {

using Keypad = ThetaGP::Drivers::Device::Keypad;

Gamepad::Gamepad() {
  setup();
}

void Gamepad::setup() {
  _state = GamepadState{};
  _inputDevice = nullptr;
  _initialized = true;
  _ready = false;
}

void Gamepad::reinit() { setup(); }

/**
 * @brief Register a keypad device
 * @param device Reference to Device (must be Keypad)
 */
void Gamepad::registerKeypadDevice(Device &device) {
  if (device.getType() == DeviceType::Keypad) {
    _inputDevice = &device;
    _ready = true;
  }
}

/**
 * @brief Set button mapping
 * @param physicalKeyId Keypad physical key index (0-31)
 * @param gamepadButtonIndex Gamepad button index (0-31)
 */
void Gamepad::setMapping(uint8_t physicalKeyId, uint8_t gamepadButtonIndex) {
  if (physicalKeyId < 32) {
    _mappings[physicalKeyId] = gamepadButtonIndex;
  }
}

void Gamepad::setButtonMappings() {
  // Clear all mappings (0xFF = unmapped)
  _mappings.fill(0xFF);

#ifdef KEYPAD_BUTTON_MAP
  constexpr struct { uint8_t key; uint32_t mask; } map[] = { KEYPAD_BUTTON_MAP };
  for (auto &entry : map) {
    if (entry.mask != 0) {
      setMapping(entry.key, __builtin_ctz(entry.mask));
    }
  }
#else
#error "necessary.keypad.button_map must be defined in BoardConfig.lua"
#endif
}

/**
 * @brief Read input from registered keypad device
 */
void Gamepad::read() {
  if (!_inputDevice || !_inputDevice->isInitialized()) {
    return;
  }

  Keypad &keypad = reinterpret_cast<Keypad &>(_inputDevice);

  _state.buttons = 0;
  _state.dpad = 0;

  uint32_t keypadMask = keypad.getPressedMaskValue();

  // Process all 32 physical keys
  for (uint8_t i = 0; i < 32; i++) {
    if (keypadMask & (1U << i)) {
      uint8_t buttonIndex = _mappings[i];
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
}

} // namespace ThetaGP::Gamepad
