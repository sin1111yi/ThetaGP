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

#include "drivers/device/device.h"
#include "drivers/peripherals/gpio.h"
#include "drivers/peripherals/timer.h"

#include <array>

namespace ThetaGP::Drivers::Device {

using Peripheral::GPIO::Pin;
using Peripheral::GPIO::PinDesc;
using Peripheral::GPIO::Port;
using Peripheral::TIMER::HardwareTimer;

enum class KeyState : bool {
  Released = 0,
  Pressed = 1,
};

// Key index representing no key at this position
static constexpr uint8_t KEYPAD_NO_KEY = 0xFF;

struct KeypadConfig {
  static constexpr uint32_t DEFAULT_SCAN_FREQ = 20000;
  static constexpr uint8_t DEBOUNCE_SAMPLES = 16;
  static constexpr uint8_t DEBOUNCE_THRESHOLD = 12;
  static constexpr uint32_t GPIO_STABILIZE_DELAY_CYCLES = 50;

  enum class Mode : uint8_t {
    ScanMatrix,
    IODirect,
    SpiDriven74HC165,
  };

  enum class Active : uint8_t {
    None,
    Low,
    High,
  };
};

class Keypad : public Device {
private:
  Keypad();

#ifndef KEYPAD_KEY_MAP
#error "necessary.keypad.key_map must be defined in BoardConfig.lua"
#endif

#ifndef KEYPAD_DRIVE_MODE
#error "necessary.keypad.drive_mode must be defined in BoardConfig.lua"
#endif

#ifndef KEYPAD_ACTIVE_MODE
#error "necessary.keypad.active_mode must be defined in BoardConfig.lua"
#endif

  static constexpr auto _keyMap =
      std::array<uint8_t, KEYPAD_KEY_MAP_SIZE>{KEYPAD_KEY_MAP};
  static constexpr size_t DRIVE_PIN_NUM = KEYPAD_DRIVE_PIN_NUM;
  static constexpr size_t SENSE_PIN_NUM = KEYPAD_SENSE_PIN_NUM;
  static constexpr size_t MAX_KEY_INDEX = KEYPAD_MAX_KEY_INDEX;
  static constexpr size_t MASK_ARRAY_SIZE = 1; // 32 keys = 1 uint32_t
  static constexpr size_t MAX_KEYS = 32;

  struct KeySampler {
    uint16_t history = 0;
    KeyState stableState = KeyState::Released;
  };

  std::array<KeySampler, MAX_KEYS> _samplers;
  uint8_t _scanCount = 0;

  volatile uint32_t _pressedMask = 0;
  HardwareTimer _scanTimer;

  static constexpr KeypadConfig::Mode _mode = KEYPAD_DRIVE_MODE;
  static constexpr KeypadConfig::Active _active = KEYPAD_ACTIVE_MODE;

  std::array<PinDesc, KEYPAD_DRIVE_PIN_NUM> _drivePins;
  std::array<PinDesc, KEYPAD_SENSE_PIN_NUM> _sensePins;

  using InputReader = void (Keypad::*)(uint32_t *);
  InputReader _readInput = nullptr;

  [[maybe_unused]] void readInputScanMatrix(uint32_t *mask);
  [[maybe_unused]] void readInputIODirect(uint32_t *mask);
  [[maybe_unused]] void readInputSpiDriven74HC165(uint32_t *mask);

  void initPins();
  void scanCallback();
  static void timerCallback(void *context);

public:
  static Keypad &getInstance() {
    static Keypad instance;
    return instance;
  }

  void init() override;

  uint32_t getPressed() const;

  bool isKeyPressed(uint8_t keyId) const;

  static constexpr uint8_t getKeyId(uint8_t driveIdx, uint8_t senseIdx) {
    if (driveIdx >= DRIVE_PIN_NUM || senseIdx >= SENSE_PIN_NUM)
      return KEYPAD_NO_KEY;
    const uint8_t physicalIndex = driveIdx * SENSE_PIN_NUM + senseIdx;
    return _keyMap[physicalIndex];
  }

  static constexpr void getKeyPosition(uint8_t keyId, uint8_t &driveIdx,
                                       uint8_t &senseIdx) {
    driveIdx = 0xFF;
    senseIdx = 0xFF;
    for (uint8_t d = 0; d < DRIVE_PIN_NUM; d++) {
      for (uint8_t s = 0; s < SENSE_PIN_NUM; s++) {
        if (getKeyId(d, s) == keyId) {
          driveIdx = d;
          senseIdx = s;
          return;
        }
      }
    }
  }

  static constexpr bool isValidKey(uint8_t keyId) {
    return keyId != KEYPAD_NO_KEY && keyId < MAX_KEYS;
  }

  static constexpr size_t getMaxKeyId() { return MAX_KEY_INDEX; }
  static constexpr size_t getMaskArraySize() { return MASK_ARRAY_SIZE; }
};

} // namespace ThetaGP::Drivers::Device
