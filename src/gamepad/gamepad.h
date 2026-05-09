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

#include "BoardConfig.h"

#include "utils/time.h"
#include "utils/types.h"

#include "drivers/device/devicemgr.h"
#include "drivers/gpdriver/gpdriver.h"
#include "drivers/gpdriver/gpdrivermgr.h"

#include "gamepad/gamepadstate.h"

#include <array>

namespace ThetaGP::Gamepad {

using Device = ThetaGP::Drivers::Device::Device;
using GPDriverManager = ThetaGP::Drivers::GPDriver::GPDriverManager;

/**
 * @brief Gamepad class - handles input mapping and state management
 *
 * Process method is designed to be called by task scheduler
 */
class Gamepad {
private:
  GamepadState _state;
  std::array<uint8_t, 32>
      _mappings; // Physical key → Gamepad button (0xFF = unmapped)
  Device *_inputDevice = nullptr;
  bool _initialized = false;
  bool _ready = false;
  GPDriverManager *_gpDriverMgr = nullptr;

  void read();

  // Helper methods (GP2040-CE style)
  [[nodiscard]] inline bool __attribute__((always_inline))
  pressedButton(const uint32_t mask) const {
    return (_state.buttons & mask) == mask;
  }

  [[nodiscard]] inline bool __attribute__((always_inline))
  pressedDpad(const uint8_t mask) const {
    return (_state.dpad & mask) == mask;
  }

public:
  Gamepad();

  void setup();
  void reinit();
  void process();

  static Gamepad &getInstance() {
    static Gamepad instance;
    return instance;
  }

  /**
   * @brief Register a keypad device
   * @param device Reference to Device (must be Keypad)
   */
  void registerKeypadDevice(Device &device);

  /**
   * @brief Set button mapping
   * @param physicalKeyId Keypad physical key index (0-31)
   * @param gamepadButtonIndex Gamepad button index (0-31)
   */
  void setMapping(uint8_t physicalKeyId, uint8_t gamepadButtonIndex);

  void setButtonMappings();

  /* clang-format off */
  // GP2040-CE style button query methods (forced inline for performance)
  #define PRESS_FUNC_ATTR [[nodiscard]] __attribute__((always_inline)) inline

  PRESS_FUNC_ATTR bool pressedUp() const { return pressedDpad(GAMEPAD_MASK_UP); }
  PRESS_FUNC_ATTR bool pressedDown() const { return pressedDpad(GAMEPAD_MASK_DOWN); }
  PRESS_FUNC_ATTR bool pressedLeft() const { return pressedDpad(GAMEPAD_MASK_LEFT); }
  PRESS_FUNC_ATTR bool pressedRight() const { return pressedDpad(GAMEPAD_MASK_RIGHT); }
  PRESS_FUNC_ATTR bool pressedB1() const { return pressedButton(GAMEPAD_MASK_B1); }
  PRESS_FUNC_ATTR bool pressedB2() const { return pressedButton(GAMEPAD_MASK_B2); }
  PRESS_FUNC_ATTR bool pressedB3() const { return pressedButton(GAMEPAD_MASK_B3); }
  PRESS_FUNC_ATTR bool pressedB4() const { return pressedButton(GAMEPAD_MASK_B4); }
  PRESS_FUNC_ATTR bool pressedL1() const { return pressedButton(GAMEPAD_MASK_L1); }
  PRESS_FUNC_ATTR bool pressedR1() const { return pressedButton(GAMEPAD_MASK_R1); }
  PRESS_FUNC_ATTR bool pressedL2() const { return pressedButton(GAMEPAD_MASK_L2); }
  PRESS_FUNC_ATTR bool pressedR2() const { return pressedButton(GAMEPAD_MASK_R2); }
  PRESS_FUNC_ATTR bool pressedS1() const { return pressedButton(GAMEPAD_MASK_S1); }
  PRESS_FUNC_ATTR bool pressedS2() const { return pressedButton(GAMEPAD_MASK_S2); }
  PRESS_FUNC_ATTR bool pressedL3() const { return pressedButton(GAMEPAD_MASK_L3); }
  PRESS_FUNC_ATTR bool pressedR3() const { return pressedButton(GAMEPAD_MASK_R3); }
  PRESS_FUNC_ATTR bool pressedA1() const { return pressedButton(GAMEPAD_MASK_A1); }
  PRESS_FUNC_ATTR bool pressedA2() const { return pressedButton(GAMEPAD_MASK_A2); }
  PRESS_FUNC_ATTR bool pressedA3() const { return pressedButton(GAMEPAD_MASK_A3); }
  PRESS_FUNC_ATTR bool pressedA4() const { return pressedButton(GAMEPAD_MASK_A4); }
  PRESS_FUNC_ATTR bool pressedE1() const { return pressedButton(GAMEPAD_MASK_E1); }
  PRESS_FUNC_ATTR bool pressedE2() const { return pressedButton(GAMEPAD_MASK_E2); }
  PRESS_FUNC_ATTR bool pressedE3() const { return pressedButton(GAMEPAD_MASK_E3); }
  PRESS_FUNC_ATTR bool pressedE4() const { return pressedButton(GAMEPAD_MASK_E4); }
  PRESS_FUNC_ATTR bool pressedE5() const { return pressedButton(GAMEPAD_MASK_E5); }
  PRESS_FUNC_ATTR bool pressedE6() const { return pressedButton(GAMEPAD_MASK_E6); }
  PRESS_FUNC_ATTR bool pressedE7() const { return pressedButton(GAMEPAD_MASK_E7); }
  PRESS_FUNC_ATTR bool pressedE8() const { return pressedButton(GAMEPAD_MASK_E8); }
  #undef PRESS_FUNC_ATTR
  /* clang-format on */

  // Get state
  [[nodiscard]] const GamepadState &getState() const { return _state; }
  [[nodiscard]] GamepadState &getState() { return _state; }

  // Check if ready
  [[nodiscard]] bool isReady() const { return _ready && _initialized; }
};

} // namespace ThetaGP::Gamepad
