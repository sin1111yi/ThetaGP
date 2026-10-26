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
#include "conf/ThetaGP_Config.h"

#include "configs/config_keys.gen.h"
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
  static constexpr uint32_t DEFAULT_SCAN_FREQ = THETAGP_CFG_KEYPAD_SCAN_HZ;

  // Consecutive scans of one level that a key's line has to hold before its
  // committed state follows it, one threshold per direction. These are run
  // lengths, not window sizes: a sample that agrees with the committed state
  // clears both runs, so a level change commits exactly that many scans after the
  // last opposed sample.
  //
  // A profile carries the pair (`sys.deb_samp` and `sys.deb_thr`) and both are read
  // on every scan. A key is sampled once per scan, so the committed state refreshes
  // at THETAGP_CFG_KEYPAD_SCAN_HZ. The thresholds count scans, so their meaning in
  // time moves with the scan rate and the report rate, which is what the assertions
  // below pin at compile time.

  // The NOPs between driving a line and reading the columns: 50 cycles, about
  // 0.1 us at 480 MHz. The default assumes a 30-50 kohm pull-up against 10-20 pF;
  // a board that changes the pull-up or lengthens the trace recomputes it.
  static constexpr uint32_t GPIO_STABILIZE_DELAY_CYCLES = 50;

  // The bounds the two rows declare. A press is confirmed inside one report
  // period, and a release window is at least one report period long and at most
  // one millisecond longer than the press it follows.
  static constexpr uint32_t PRESS_MAX =
      Gamepad::Config::keyEntry(Gamepad::Config::ConfigKey::SysDebSamp).maxVal;
  static constexpr uint32_t PRESS_MIN =
      Gamepad::Config::keyEntry(Gamepad::Config::ConfigKey::SysDebSamp).minVal;
  static constexpr uint32_t RELEASE_MAX =
      Gamepad::Config::keyEntry(Gamepad::Config::ConfigKey::SysDebThr).maxVal;
  static constexpr uint32_t RELEASE_MIN =
      Gamepad::Config::keyEntry(Gamepad::Config::ConfigKey::SysDebThr).minVal;

  // The confirmation has to complete inside one report period, or a report tick
  // misses it. A run longer than that is clamped at use, so only the shortest value
  // the row takes has to fit here.
  static_assert(PRESS_MIN * THETAGP_CFG_USB_REPORT_RATE_HZ <=
                    THETAGP_CFG_KEYPAD_SCAN_HZ,
                "keypad: a press is committed only after PRESS_SAMPLES "
                "consecutive scans of the pressed level, and every scan is a "
                "decision point, so the confirmation takes PRESS_SAMPLES scan "
                "periods. It has to complete inside one report period, or a "
                "report tick can miss the confirmation entirely. Raise "
                "THETAGP_CFG_KEYPAD_SCAN_HZ or lower the board's [usb] "
                "wired_report_hz.");

  static_assert(RELEASE_MIN >= PRESS_MAX,
                "keypad: the release range has to start at or above where the "
                "press range ends. A release run shorter than a press run is "
                "the easier direction to trigger, so a held key gets chopped in "
                "half by its own bounce. Raise the floor of sys.deb_thr or "
                "lower the ceiling of sys.deb_samp.");

  static_assert(PRESS_MIN >= 2 && RELEASE_MIN >= 2,
                "keypad: both thresholds have to be at least 2 samples, so that "
                "no single sample can change a key's committed state. At 1 a "
                "threshold filters nothing: one sample commits, it stays "
                "committed until the opposite threshold is reached, and a "
                "single sampling artifact becomes a host-visible key event.");

  static_assert(RELEASE_MAX <= 255,
                "keypad: a key's run counters are uint8_t, so a threshold above "
                "255 would wrap and the key would never leave its committed "
                "state. Keep both ranges inside the counter's range.");

  static_assert(1000 * (RELEASE_MAX - PRESS_MIN) <=
                    THETAGP_CFG_KEYPAD_SCAN_HZ,
                "keypad: the release threshold stretches the reported duration "
                "of a press by (RELEASE_SAMPLES - PRESS_SAMPLES) scan periods "
                "against its physical duration. That stretch is capped at 1 ms, "
                "a tenth of the shortest tap a hand performs, and the widest "
                "pair the ranges allow exceeds the cap. Lower the range of "
                "sys.deb_thr or sys.deb_samp, or raise "
                "THETAGP_CFG_KEYPAD_SCAN_HZ.");

  static_assert(1000 * RELEASE_MIN >= THETAGP_CFG_KEYPAD_SCAN_HZ,
                "keypad: the release confirmation window has to be at least "
                "1 ms, the report period of the slowest link the product "
                "declares. Below it a short committed press can fall between "
                "two report ticks and be dropped. Raise the floor of "
                "sys.deb_thr or lower THETAGP_CFG_KEYPAD_SCAN_HZ.");

  static_assert(RELEASE_MIN * THETAGP_CFG_USB_REPORT_RATE_HZ >=
                    THETAGP_CFG_KEYPAD_SCAN_HZ,
                "keypad: the release confirmation window has to cover the "
                "report period this build runs at, or a minimum-length press "
                "falls between two report ticks and is dropped. Raise the floor "
                "of sys.deb_thr or raise the board's [usb] wired_report_hz.");

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

#ifndef BDCFG_KEYPAD_KEY_MAP
#error "[keypad] key_map is required — see configs/CONFIGURATION.md"
#endif

#ifndef BDCFG_KEYPAD_DRIVE_MODE
#error "[keypad] drive_mode is required — see configs/CONFIGURATION.md"
#endif

#ifndef BDCFG_KEYPAD_ACTIVE_MODE
#error "[keypad] active_mode is required — see configs/CONFIGURATION.md"
#endif

  static constexpr size_t DRIVE_PIN_NUM = BDCFG_KEYPAD_DRIVE_PIN_NUM;
  static constexpr size_t SENSE_PIN_NUM = BDCFG_KEYPAD_SENSE_PIN_NUM;
  static constexpr auto _keyMap =
      std::array<std::array<uint8_t, SENSE_PIN_NUM>, DRIVE_PIN_NUM>{{BDCFG_KEYPAD_KEY_MAP}};
  static constexpr size_t MAX_KEY_INDEX = BDCFG_KEYPAD_MAX_KEY_INDEX;
  static constexpr size_t MASK_ARRAY_SIZE = 1; // 32 keys = 1 uint32_t
  static constexpr size_t MAX_KEYS = 32;

  // One key's committed state and the two run counters behind it: the number of
  // consecutive samples that have opposed that state, one counter per direction.
  struct KeySampler {
    uint8_t pressRun = 0;
    uint8_t releaseRun = 0;
    KeyState stableState = KeyState::Released;
  };

  std::array<KeySampler, MAX_KEYS> _samplers;

  volatile uint32_t _pressedMask = 0;
  HardwareTimer _scanTimer;

  // ── scan time ──
  // The clock advanced across one scanCallback, read at entry and at exit. Nothing
  // else runs in the ISR, so the measurement costs about one clock read per scan and
  // the host does the subtraction.
  //
  // Counts, not microseconds: converting at the stamp would round every reading.
  // Cumulative since boot and never reset, and 64-bit because the sum grows by about
  // 4.8e8 counts per wall second at the current scan rate.
  volatile uint32_t _scanCyclesCount = 0;
  volatile uint32_t _scanCyclesLast = 0;
  volatile uint32_t _scanCyclesMax = 0;
  volatile uint64_t _scanCyclesSum = 0;

  // ── Commit counters ──
  // The commits this keypad has made, and the longest run one took. Both are
  // written only where a key's committed state changes, and the run is counted
  // in scans: one scan is one sample and one decision point.
  volatile uint32_t _commitCount = 0;
  volatile uint8_t _maxCommitLatencyScans = 0;

  static constexpr KeypadConfig::Mode _mode = BDCFG_KEYPAD_DRIVE_MODE;
  static constexpr KeypadConfig::Active _active = BDCFG_KEYPAD_ACTIVE_MODE;

  std::array<PinDesc, BDCFG_KEYPAD_DRIVE_PIN_NUM> _drivePins;
  std::array<PinDesc, BDCFG_KEYPAD_SENSE_PIN_NUM> _sensePins;

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

  // The scan-time counters above, in cycles, taken at one instant with the scan
  // interrupt masked. The consumer converts them.
  struct ScanStats {
    uint32_t count;
    uint32_t last_cycles;
    uint32_t max_cycles;
    uint64_t sum_cycles;
  };

  void getScanStats(ScanStats &out) const;

  // Readout of the commit counters above. `count` is every commit since boot,
  // press and release together; `max_latency_scans` is the longest a level
  // change ever took to commit, in scans. The snapshot is taken with the scan
  // interrupt masked, so both describe one instant.
  struct CommitStats {
    uint32_t count;
    uint8_t max_latency_scans;
  };

  void getCommitStats(CommitStats &out) const;

  static constexpr uint8_t getKeyId(uint8_t driveIdx, uint8_t senseIdx) {
    if (driveIdx >= DRIVE_PIN_NUM || senseIdx >= SENSE_PIN_NUM)
      return KEYPAD_NO_KEY;
    return _keyMap[driveIdx][senseIdx];
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
  static constexpr size_t getDriveLineCount() { return DRIVE_PIN_NUM; }
  static constexpr size_t getSenseLineCount() { return SENSE_PIN_NUM; }
};

} // namespace ThetaGP::Drivers::Device
