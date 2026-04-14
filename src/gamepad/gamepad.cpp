#include "gamepad/gamepad.h"
#include "drivers/device/keypad.h"

namespace ThetaGP::Gamepad {

using Keypad = ThetaGP::Drivers::Device::Keypad;

Gamepad::Gamepad() : _inputDevice(nullptr), _initialized(false), _ready(false) {
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

/**
 * @brief Set default mappings
 *
 * Default layout:
 * Keys 0-3   → B1, B2, B3, B4 (face buttons)
 * Keys 4-5   → L1, R1 (shoulder buttons)
 * Keys 6-7   → S1, S2 (Select, Start)
 * Keys 8-9   → L3, R3 (stick press)
 * Keys 10-11 → A1, A2 (Guide, Capture)
 * Keys 12-15 → Up, Down, Left, Right (D-pad)
 */
void Gamepad::setDefaultMappings() {
  // Clear all mappings (0xFF = unmapped)
  _mappings.fill(0xFF);

  // Face buttons
  setMapping(0, 4); // B1
  setMapping(1, 5); // B2
  setMapping(2, 6); // B3
  setMapping(3, 7); // B4

  // Shoulder buttons
  setMapping(4, 8); // L1
  setMapping(5, 9); // R1

  // Special buttons
  setMapping(6, 12); // S1 (Select)
  setMapping(7, 13); // S2 (Start)
  setMapping(8, 14); // L3
  setMapping(9, 15); // R3

  // Extra buttons
  setMapping(10, 16); // A1 (Guide/Home)
  setMapping(11, 17); // A2 (Capture)

  // D-pad (if using physical D-pad buttons)
  setMapping(12, 0); // Up
  setMapping(13, 1); // Down
  setMapping(14, 2); // Left
  setMapping(15, 3); // Right
}

/**
 * @brief Read input from registered keypad device
 */
void Gamepad::read() {
  if (!_inputDevice || !_inputDevice->isInitialized()) {
    return;
  }

  Keypad *keypad = static_cast<Keypad *>(_inputDevice);

  _state.buttons = 0;
  _state.dpad = 0;

  uint32_t keypadMask = keypad->getPressedMaskValue();

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
