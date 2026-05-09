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

#include "BoardConfig.h"

#include "drivers/device/keypad.h"

#include "drivers/peripherals/gpio.h"
#include "drivers/peripherals/peripheralsmgr.h"

#include "utils/log/log.h"

namespace ThetaGP::Drivers::Device {

using namespace Peripheral::GPIO;
using namespace Peripheral::TIMER;

Keypad::Keypad() : Device("keypad") {}

void Keypad::init() {
  if (_initialized)
    return;

  initPins();

  _readInput = &Keypad::readInputScanMatrix;

  auto setupTimer = [this](HardwareTimer &timer,
                           Peripheral::TIMER::Instance instance) {
    timer.config(instance, KeypadConfig::DEFAULT_SCAN_FREQ);
    timer.setCallback(
        [](void *self) {
          auto *keypad = static_cast<Keypad *>(self);
          if (keypad)
            keypad->scanCallback();
        },
        this);
    timer.init();
    return timer.isInitialized();
  };

  setupTimer(_scanTimer, Peripheral::PeripheralsManager::getInstance().reservedTimer());

  _scanTimer.start();
  _initialized = true;
}

void Keypad::scanCallback() {
  uint32_t mask = 0;
  (this->*_readInput)(&mask);

  // Update sample history for all 32 keys
  for (size_t i = 0; i < MAX_KEYS; i++) {
    KeySampler &s = _samplers[i];
    const bool pressed = (mask & (1U << i)) != 0;
    s.history = ((s.history << 1) | pressed) & 0xFFFF;
  }

  _scanCount = (_scanCount + 1) % KeypadConfig::DEBOUNCE_SAMPLES;
  if (_scanCount != 0) {
    return;
  }

  // Majority vote and update state
  uint32_t debouncedMask = 0;

  for (size_t i = 0; i < MAX_KEYS; i++) {
    KeySampler &s = _samplers[i];
    const uint8_t count = __builtin_popcount(s.history);
    s.stableState = count >= KeypadConfig::DEBOUNCE_THRESHOLD
                        ? KeyState::Pressed
                        : KeyState::Released;

    if (s.stableState == KeyState::Pressed) {
      debouncedMask |= (1U << i);
    }
  }

  _pressedMask = debouncedMask;
}

void Keypad::readInputScanMatrix(uint32_t *mask) {
  const bool activeLow = (_active == KeypadConfig::Active::Low);
  const PinState driveState = activeLow ? PinState::Reset : PinState::Set;
  const PinState idleState = activeLow ? PinState::Set : PinState::Reset;
  const PinState senseState = driveState;

  for (size_t d = 0; d < DRIVE_PIN_NUM; d++) {
    Gpio driveGpio(_drivePins[d]);
    driveGpio.write(driveState);

    for (uint32_t i = 0; i < KeypadConfig::GPIO_STABILIZE_DELAY_CYCLES; i++) {
      __NOP();
    }

    for (size_t s = 0; s < SENSE_PIN_NUM; s++) {
      Gpio senseGpio(_sensePins[s]);
      if (senseGpio.read() == senseState) {
        const uint8_t keyId = getKeyId(d, s);
        if (isValidKey(keyId)) {
          *mask |= (1U << keyId);
        }
      }
    }

    driveGpio.write(idleState);
  }
}

void Keypad::initPins() {
#if defined(KEYPAD_DRIVE_IO_LIST) && defined(KEYPAD_SENSE_IO_LIST)
  const PinDesc drivePinsTmp[] = {KEYPAD_DRIVE_IO_LIST};
  const PinDesc sensePinsTmp[] = {KEYPAD_SENSE_IO_LIST};

  const bool activeLow = (_active == KeypadConfig::Active::Low);
  const PinState idleState = activeLow ? PinState::Set : PinState::Reset;
  const Pull sensePull = activeLow ? Pull::PullUp : Pull::PullDown;

  for (size_t i = 0; i < DRIVE_PIN_NUM; i++) {
    _drivePins[i] = drivePinsTmp[i];
    Gpio gpio(_drivePins[i]);
    gpio.config(Mode::OutputPushPull, Pull::NoPull, Speed::VeryHigh);
    gpio.init();
    gpio.write(idleState);
  }

  for (size_t i = 0; i < SENSE_PIN_NUM; i++) {
    _sensePins[i] = sensePinsTmp[i];
    Gpio gpio(_sensePins[i]);
    gpio.config(Mode::Input, sensePull, Speed::High);
    gpio.init();
  }
#endif
}

uint32_t Keypad::getPressed() const {
  UNUSED(this);
  return _pressedMask;
}

bool Keypad::isKeyPressed(uint8_t keyId) const {
  if (keyId >= MAX_KEYS)
    return false;
  return (_pressedMask & (1U << keyId)) != 0;
}

} // namespace ThetaGP::Drivers::Device
