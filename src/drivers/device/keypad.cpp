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
#include "conf/ThetaGP_Config.h" // THETAGP_CFG_KEY_TOGGLE_EN, the keypad toggle switch

#include "drivers/device/keypad.h"

#include "drivers/device/systimer.h"
#include "drivers/peripherals/gpio.h"
#include "drivers/peripherals/nvic.h"
#include "drivers/peripherals/peripheralsmgr.h"
#include "drivers/peripherals/systick.h"

#include "utils/atomic.h"
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
  // Minimum measurement point: one read of the device layer's cycle counter at
  // each end of the callback. SystemTimer is how the rest of the device layer
  // takes time (the scheduler reads its clock the same way), and the keypad is a
  // device, so it takes the time from the device-layer timer rather than from a
  // peripheral register. The counter form is the one to use here: its tick is
  // one CPU cycle, so a callback that lasts ten microseconds is measured to the
  // cycle, while the microsecond clock would quantize it to ten steps and cost
  // an atomic block and a division per read. Both ends are one read plus one
  // call, and the host converts to microseconds where a conversion is wanted.
  const uint32_t startCycles = SystemTimer::getInstance().getCycleCounter();

  uint32_t mask = 0;
  (this->*_readInput)(&mask);

  // Per-scan decision, one key at a time, from the raw mask this scan read.
  // A sample that agrees with a key's committed state clears both of its runs,
  // so a run of the opposite level counts only while it stays unbroken and a
  // burst shorter than the threshold that guards that direction leaves no trace
  // at all. A run that reaches its threshold commits the level it counted and
  // then starts over.
  uint32_t committedMask = 0;

  for (size_t i = 0; i < MAX_KEYS; i++) {
    KeySampler &s = _samplers[i];
    const bool pressed = (mask & (1U << i)) != 0;
    const bool stable = (s.stableState == KeyState::Pressed);

    if (pressed == stable) {
      s.pressRun = 0;
      s.releaseRun = 0;
    } else if (pressed) {
      s.releaseRun = 0;
      if (++s.pressRun >= KeypadConfig::PRESS_SAMPLES) {
        s.stableState = KeyState::Pressed;
        s.pressRun = 0;
      }
    } else {
      s.pressRun = 0;
      if (++s.releaseRun >= KeypadConfig::RELEASE_SAMPLES) {
        s.stableState = KeyState::Released;
        s.releaseRun = 0;
      }
    }

    if (s.stableState == KeyState::Pressed) {
      committedMask |= (1U << i);
    }
  }

  // The committed mask is assembled whole and published once, so a reader sees
  // either the previous mask or this one, never a mixture of two keys.
  _pressedMask = committedMask;

  // Raw cycles, kept unconverted so the counters hold the measurement at full
  // resolution; the readout converts. The pair of stamps costs a few cycles and
  // is not subtracted out — at this scale the measurement is about five thousand
  // cycles, so the cost is well under a tenth of a percent of it.
  const uint32_t elapsedCycles = SystemTimer::getInstance().getCycleCounter() - startCycles;
  _scanCyclesLast = elapsedCycles;
  _scanCyclesSum += elapsedCycles;
  if (elapsedCycles > _scanCyclesMax) {
    _scanCyclesMax = elapsedCycles;
  }
  _scanCyclesCount++;
}

void Keypad::getScanStats(ScanStats &out) const {
  ATOMIC_BLOCK(NVIC_PRIO_MAX) {
    out.count = _scanCyclesCount;
    out.last_cycles = _scanCyclesLast;
    out.max_cycles = _scanCyclesMax;
    out.sum_cycles = _scanCyclesSum;
  }
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
#if defined(BDCFG_KEYPAD_DRIVE_IO_LIST) && defined(BDCFG_KEYPAD_SENSE_IO_LIST)
  const PinDesc drivePinsTmp[] = {BDCFG_KEYPAD_DRIVE_IO_LIST};
  const PinDesc sensePinsTmp[] = {BDCFG_KEYPAD_SENSE_IO_LIST};

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
#if THETAGP_CFG_KEY_TOGGLE_EN
  // Test hook: flip one key bit per read. The gamepad tick reads this once, so
  // the mask differs from the previous report on every tick. A free-running
  // counter is used rather than a timer bit because the tick period is an even
  // number of microseconds, which would keep a micros()-derived bit's parity
  // from tick to tick.
  static uint32_t s_readCount = 0;
  return _pressedMask ^ ((++s_readCount & 1U) ? 0x1U : 0x2U);
#else
  return _pressedMask;
#endif
}

bool Keypad::isKeyPressed(uint8_t keyId) const {
  if (keyId >= MAX_KEYS)
    return false;
  return (_pressedMask & (1U << keyId)) != 0;
}

} // namespace ThetaGP::Drivers::Device
