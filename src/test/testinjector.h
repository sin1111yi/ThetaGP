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

#include <cstdint>

#include "gamepad/gamepadstate.h"
#include "drivers/gpdriver/hid/HIDDescriptors.h"

namespace ThetaGP::Test {

#ifdef THETAGP_ENABLE_TEST_API

/**
 * TestInjector — GamepadRawInput / HIDReport hook for test state injection and capture.
 *
 * Registers as a listener on two hook points in the main processing pipeline:
 *   gamepadRawInputHook (onGamepadRawInput): called after Gamepad::read()
 *   hidReportHook (onHIDReport):       called before tud_hid_report()
 *
 * Supports three modes:
 *   PASS_THRU — pass data through unchanged, optionally record history
 *   INJECT    — replace data from inject queue, record history
 *   RECORD    — pass data through, always record history
 *
 * Also supports override mode for persistent replacement of
 * gamepad state or HID report fields.
 */
class TestInjector {
public:
    static TestInjector &getInstance();

    /// Static wrapper for GamepadRawInputHook registration
    static void gamepadRawInputHook(Gamepad::GamepadRawInput &state) {
        getInstance().onGamepadRawInput(state);
    }

    /// Static wrapper for HIDReportHook registration
    static void hidReportHook(HIDReport &report) {
        getInstance().onHIDReport(report);
    }

    // Called after Gamepad::read(), can modify state
    void onGamepadRawInput(Gamepad::GamepadRawInput &state);

    // Called before tud_hid_report(), can modify report
    void onHIDReport(HIDReport &report);

    // --- Inject queue access (used by TestCmdHandler) ---
    bool injectGamepadRawInput(const Gamepad::GamepadRawInput &state);
    bool injectHIDReport(const HIDReport &report);
    void clearGamepadRawInputQueue();
    void clearHIDInjectQueue();
    uint8_t gamepadRawInputQueueCount() const { return _injectGpCount; }
    uint8_t hidInjectQueueCount() const { return _injectHidCount; }

    // --- Override control ---
    void setOverrideGamepadRawInput(bool enabled);
    void setOverrideHIDReport(bool enabled);
    bool isOverrideGamepadRawInput() const { return _overrideGamepadRawInput; }
    bool isOverrideHIDReport() const { return _overrideHIDReport; }

    // --- Mode control ---
    enum class TestMode { PASS_THRU = 0, INJECT = 1, RECORD = 2 };

    void setMode(TestMode mode);
    TestMode getMode() const { return _mode; }
    static const char *modeName(TestMode mode);

    // --- Snapshot access ---
    const Gamepad::GamepadRawInput &getCurrentGamepadRawInput() const { return _currentGamepadRawInput; }
    const HIDReport &getCurrentHIDReport() const { return _currentHIDReport; }

    // --- History access ---
    struct GamepadRawInputEntry {
        uint32_t timestampUs;
        Gamepad::GamepadRawInput state;
    };

    struct HIDReportEntry {
        uint32_t timestampUs;
        HIDReport report;
    };

    uint8_t getGamepadHistoryCount() const { return _gpHistoryCount; }
    uint8_t getHIDHistoryCount() const { return _hidHistoryCount; }

    /**
     * Copy up to `maxCount` entries from the gamepad history ring buffer.
     * Returns the number of entries actually copied (newest first).
     */
    uint8_t readGamepadHistory(GamepadRawInputEntry *out, uint8_t maxCount) const;

    /**
     * Copy up to `maxCount` entries from the HID history ring buffer.
     * Returns the number of entries actually copied (newest first).
     */
    uint8_t readHIDHistory(HIDReportEntry *out, uint8_t maxCount) const;

    void clearGamepadHistory();
    void clearHIDHistory();

    // --- Full reset ---
    void reset();

private:
    TestInjector() = default;
    TestInjector(const TestInjector &) = delete;
    TestInjector &operator=(const TestInjector &) = delete;

    void recordGamepadRawInput(const Gamepad::GamepadRawInput &state);
    void recordHIDReport(const HIDReport &report);

    // Test mode
    TestMode _mode = TestMode::PASS_THRU;

    // Override flags
    bool _overrideGamepadRawInput = false;
    bool _overrideHIDReport = false;

    // Inject queue (FIFO, depth 8)
    static constexpr uint8_t INJECT_QUEUE_DEPTH = 8;
    Gamepad::GamepadRawInput _injectedGamepadRawInputs[INJECT_QUEUE_DEPTH];
    HIDReport _injectedHIDReports[INJECT_QUEUE_DEPTH];
    uint8_t _injectGpHead = 0;
    uint8_t _injectGpTail = 0;
    uint8_t _injectGpCount = 0;
    uint8_t _injectHidHead = 0;
    uint8_t _injectHidTail = 0;
    uint8_t _injectHidCount = 0;

    // Current snapshots (for GET commands)
    Gamepad::GamepadRawInput _currentGamepadRawInput{};
    HIDReport _currentHIDReport{};

    // History ring buffer (depth 64)
    static constexpr uint8_t HISTORY_DEPTH = 64;
    GamepadRawInputEntry _gpHistory[HISTORY_DEPTH];
    HIDReportEntry _hidHistory[HISTORY_DEPTH];
    uint8_t _gpHistoryHead = 0;
    uint8_t _gpHistoryCount = 0;
    uint8_t _hidHistoryHead = 0;
    uint8_t _hidHistoryCount = 0;
};

#else

// Production mode no-op stub
class TestInjector {
public:
    static TestInjector &getInstance() { static TestInjector i; return i; }
    void onGamepadRawInput(Gamepad::GamepadRawInput &) {}
    void onHIDReport(HIDReport &) {}
};

#endif

} // namespace ThetaGP::Test
