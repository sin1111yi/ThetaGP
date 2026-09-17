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

  // Consecutive scans of one level that a key's line has to hold before the
  // key's committed state follows it, one threshold per direction. These are
  // run lengths, not window sizes: a sample that agrees with the committed
  // state clears both runs, so a run shorter than its threshold leaves no
  // trace, and a level change commits exactly that many scans after the last
  // opposed sample — no window has to fill, and nothing is carried over from
  // before it.
  static constexpr uint8_t PRESS_SAMPLES = 2;
  static constexpr uint8_t RELEASE_SAMPLES = 32;

  static constexpr uint32_t GPIO_STABILIZE_DELAY_CYCLES = 50;

  // One scan callback walks every drive line (scanCallback -> the
  // readInputScanMatrix loop over DRIVE_PIN_NUM), so a key is sampled once per
  // scan and its sampling rate is THETAGP_CFG_KEYPAD_SCAN_HZ itself. Every scan
  // is also a decision point, so the committed-state refresh rate is that same
  // scan rate: there is no window and no vote behind it, only the two run
  // counters a key keeps. THETAGP_CFG_KEYPAD_SCAN_HZ is the adjustable value
  // (src/conf/ThetaGP_Config.h); the report rate is declared by the board
  // ([usb] wired_report_hz in its BoardConfig.toml); the drive-line count
  // follows the board's key matrix.
  //
  // The thresholds are counts of scans, so their meaning in time moves with the
  // scan rate and the report rate. The assertions below pin that meaning at
  // compile time instead of leaving it to the configuration: each one names the
  // property it protects, and a build that would break it does not compile.
  static_assert(THETAGP_CFG_KEYPAD_SCAN_HZ >=
                    PRESS_SAMPLES * THETAGP_CFG_USB_REPORT_RATE_HZ,
                "keypad: a press is committed only after PRESS_SAMPLES "
                "consecutive scans of the pressed level, and every scan is a "
                "decision point, so the confirmation takes PRESS_SAMPLES scan "
                "periods. It has to complete inside one report period, or a "
                "report tick can miss the confirmation entirely. Raise "
                "THETAGP_CFG_KEYPAD_SCAN_HZ in src/conf/ThetaGP_Config.h or "
                "lower the board's [usb] wired_report_hz; the quantity that has "
                "to fit in a report period is the confirmation, not the "
                "sampling rate.");

  static_assert(RELEASE_SAMPLES >= PRESS_SAMPLES,
                "keypad: the release threshold has to be at least the press "
                "threshold. With a shorter release run the release direction "
                "is the easier one to trigger, so a burst of the opposite level "
                "that cannot commit a press can still commit a release, and a "
                "held key gets chopped in half by its own bounce.");

  static_assert(PRESS_SAMPLES >= 2 && RELEASE_SAMPLES >= 2,
                "keypad: both thresholds have to be at least 2 samples, so that "
                "no single sample can change a key's committed state. At 1 a "
                "threshold filters nothing: one sample commits, it stays "
                "committed until the opposite threshold is reached, and a "
                "single sampling artifact becomes a host-visible key event.");

  static_assert(static_cast<uint32_t>(RELEASE_SAMPLES) <= 255,
                "keypad: a key's run counters are uint8_t, so a threshold above "
                "255 would wrap and the key would never leave its committed "
                "state. Keep both thresholds inside the counter's range.");

  static_assert(1000 * (RELEASE_SAMPLES - PRESS_SAMPLES) <=
                    THETAGP_CFG_KEYPAD_SCAN_HZ,
                "keypad: the release threshold stretches the reported duration "
                "of a press by (RELEASE_SAMPLES - PRESS_SAMPLES) scan periods "
                "against its physical duration. That stretch is capped at 1 ms, "
                "a tenth of the shortest tap a hand performs, and this pair of "
                "thresholds exceeds the cap. Lower RELEASE_SAMPLES, raise "
                "PRESS_SAMPLES, or raise THETAGP_CFG_KEYPAD_SCAN_HZ.");

  static_assert(1000 * RELEASE_SAMPLES >= THETAGP_CFG_KEYPAD_SCAN_HZ,
                "keypad: the release confirmation window is RELEASE_SAMPLES "
                "scan periods long and has to be at least 1 ms, the report "
                "period of the slowest link the product declares. Below it a "
                "short committed press can fall between two report ticks and be "
                "dropped. Raise RELEASE_SAMPLES or lower "
                "THETAGP_CFG_KEYPAD_SCAN_HZ.");

  static_assert(RELEASE_SAMPLES * THETAGP_CFG_USB_REPORT_RATE_HZ >=
                    THETAGP_CFG_KEYPAD_SCAN_HZ,
                "keypad: the release confirmation window is RELEASE_SAMPLES "
                "scan periods long and has to cover the report period this "
                "build runs at, or a minimum-length press falls between two "
                "report ticks and is dropped. Raise RELEASE_SAMPLES, lower "
                "THETAGP_CFG_KEYPAD_SCAN_HZ, or raise the board's [usb] "
                "wired_report_hz.");

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

  // One key's committed state and the two run counters behind it: the number
  // of consecutive samples that have opposed that state, one counter per
  // direction. Any sample that agrees with the state clears both, so only an
  // unbroken run of the opposite level can move the key, and the counter that
  // crossed its threshold is reset on the crossing.
  struct KeySampler {
    uint8_t pressRun = 0;
    uint8_t releaseRun = 0;
    KeyState stableState = KeyState::Released;
  };

  std::array<KeySampler, MAX_KEYS> _samplers;

  volatile uint32_t _pressedMask = 0;
  HardwareTimer _scanTimer;

  // ── Scan time (minimum measurement point) ──
  // Microseconds the device layer's clock (SystemTimer::getMicros) advanced across
  // one scanCallback: a read at entry, a read at exit, then three accumulating
  // writes. Nothing else runs in the ISR — no division, no print, no peripheral
  // access — so the measurement costs about one clock read per scan, and the host
  // does the subtraction.
  //
  // Cycles, not microseconds. The counter ticks once per CPU cycle, so a scan of
  // roughly ten microseconds reads as five thousand counts and keeps that
  // resolution; converting at the stamp would round every reading to a step of
  // about 1/480 of the value. The readout converts, and only where microseconds
  // are wanted.
  //
  // Cumulative since boot and never reset: an average is sum_cycles / count and a
  // worst case is max_cycles, both computed on the host. The sum accumulates the
  // scan's own time only, never idle time, so at the current scan rate it grows
  // by ≈4.8e8 counts per wall second — a 32-bit accumulator would wrap in about
  // nine seconds; it is 64-bit for that reason.
  volatile uint32_t _scanCyclesCount = 0;
  volatile uint32_t _scanCyclesLast = 0;
  volatile uint32_t _scanCyclesMax = 0;
  volatile uint64_t _scanCyclesSum = 0;

  // ── Commit counters (ADR-0006 O-6) ──
  // Two counters written only where a key's committed state changes, so the scan
  // path pays nothing for them: the increments sit inside the two branches that
  // already existed at the commit points, and a scan that commits nothing runs
  // no compare, no stamp and no division on their behalf. They are what turns
  // the filter's unverified bounce tolerance (ADR-0006 §8, KU-1) into a number
  // read off the board: a lost tap leaves the count where it was, a double
  // commit moves it twice for one physical action — a tap is one press commit
  // plus one release commit.
  //
  // Lateness is in scans, not microseconds. One scan is one sample and one
  // decision point, so the unit the filter works in is the count of them, and
  // the readout converts (it holds the scan rate; the driver never divides).
  // The value stamped is the run the key held when it crossed its threshold:
  // the number of consecutive samples that opposed the committed state, the
  // first of them being the sample the change was first seen in, the last being
  // the one that commits. By construction that run is exactly the direction's
  // threshold, so this is a bound the board confirms rather than a number that
  // varies — see the report for what that does and does not buy.
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

  // Readout of the scan-time counters above, in cycles. The snapshot is taken
  // with the scan interrupt masked, so all four describe the same instant. The
  // consumer converts: cyclesToMicros() for one reading, or its own division for
  // a mean off sum_cycles, which is past what the 32-bit conversion takes.
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
