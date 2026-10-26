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

#include "drivers/device/system_timer.h"
#include "gamepad/config/config_manager.h"
#include "drivers/peripherals/gpio.h"
#include "drivers/peripherals/nvic.h"
#include "drivers/peripherals/peripherals_manager.h"
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
  // One read of the device-layer cycle counter at each end of the callback: a
  // tick is one CPU cycle, so the measurement is to the cycle and the host
  // converts it where a conversion is wanted.
  const uint32_t startCycles = SystemTimer::getInstance().getCycleCounter();

  // The pair a profile carries. The longer of the two is what the release
  // threshold takes.
  const Gamepad::Config::Sys &sys =
      Gamepad::Config::ConfigManager::getInstance().config().sys;
  // A press has to be confirmed inside one report period: a run longer than the
  // scans one period holds is held to that many, whatever the body asks for.
  constexpr uint8_t kPressInOneReport =
      static_cast<uint8_t>(THETAGP_CFG_KEYPAD_SCAN_HZ /
                           THETAGP_CFG_USB_REPORT_RATE_HZ);
  const uint8_t asked = sys.deb_samp < kPressInOneReport ? sys.deb_samp
                                                         : kPressInOneReport;
  const uint8_t pressSamples = asked < 2 ? 2 : asked;
  const uint8_t releaseSamples =
      sys.deb_thr < pressSamples ? pressSamples : sys.deb_thr;

  uint32_t mask = 0;
  (this->*_readInput)(&mask);

  // One key at a time, from the raw mask this scan read: a sample that agrees
  // with a key's committed state clears both of its runs, and a run that
  // reaches its threshold commits the level it counted and starts over.
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
      if (++s.pressRun >= pressSamples) {
        s.stableState = KeyState::Pressed;
        // Commit point, press: the run that crossed its threshold is stamped
        // while the counter still holds it.
        _commitCount++;
        if (s.pressRun > _maxCommitLatencyScans) {
          _maxCommitLatencyScans = s.pressRun;
        }
        s.pressRun = 0;
      }
    } else {
      s.pressRun = 0;
      if (++s.releaseRun >= releaseSamples) {
        s.stableState = KeyState::Released;
        // Commit point, release, stamped as the press one is.
        _commitCount++;
        if (s.releaseRun > _maxCommitLatencyScans) {
          _maxCommitLatencyScans = s.releaseRun;
        }
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

  // Raw cycles, kept unconverted; the readout converts.
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

void Keypad::getCommitStats(CommitStats &out) const {
  // Both fields are read in one masked block, so the scan interrupt cannot
  // land between them.
  ATOMIC_BLOCK(NVIC_PRIO_MAX) {
    out.count = _commitCount;
    out.max_latency_scans = _maxCommitLatencyScans;
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
  // Test hook: one key bit flipped per read, so every report differs from the
  // one before it.
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
