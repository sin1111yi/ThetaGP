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
 * TestInjector — Point A/B hook for test state injection and capture.
 *
 * Provides two hook points in the main processing pipeline:
 *   Point A (onGamepadState): called after Gamepad::read()
 *   Point B (onHIDReport):    called before tud_hid_report()
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

    // Point A: called after Gamepad::read(), can modify state
    void onGamepadState(Gamepad::GamepadState &state);

    // Point B: called before tud_hid_report(), can modify report
    void onHIDReport(HIDReport &report);

    // --- Inject queue access (used by TestCmdHandler) ---
    bool injectGamepadState(const Gamepad::GamepadState &state);
    bool injectHIDReport(const HIDReport &report);
    void clearGamepadInjectQueue();
    void clearHIDInjectQueue();
    uint8_t gamepadInjectQueueCount() const { return _injectGpCount; }
    uint8_t hidInjectQueueCount() const { return _injectHidCount; }

    // --- Override control ---
    void setOverrideGamepadState(bool enabled);
    void setOverrideHIDReport(bool enabled);
    bool isOverrideGamepadState() const { return _overrideGamepadState; }
    bool isOverrideHIDReport() const { return _overrideHIDReport; }

    // --- Mode control ---
    enum class TestMode { PASS_THRU = 0, INJECT = 1, RECORD = 2 };

    void setMode(TestMode mode);
    TestMode getMode() const { return _mode; }
    static const char *modeName(TestMode mode);

    // --- Snapshot access ---
    const Gamepad::GamepadState &getCurrentGamepadState() const { return _currentGamepadState; }
    const HIDReport &getCurrentHIDReport() const { return _currentHIDReport; }

    // --- History access ---
    struct GamepadStateEntry {
        uint32_t timestampUs;
        Gamepad::GamepadState state;
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
    uint8_t readGamepadHistory(GamepadStateEntry *out, uint8_t maxCount) const;

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

    void recordGamepadState(const Gamepad::GamepadState &state);
    void recordHIDReport(const HIDReport &report);

    // Test mode
    TestMode _mode = TestMode::PASS_THRU;

    // Override flags
    bool _overrideGamepadState = false;
    bool _overrideHIDReport = false;

    // Inject queue (FIFO, depth 8)
    static constexpr uint8_t INJECT_QUEUE_DEPTH = 8;
    Gamepad::GamepadState _injectedGamepadStates[INJECT_QUEUE_DEPTH];
    HIDReport _injectedHIDReports[INJECT_QUEUE_DEPTH];
    uint8_t _injectGpHead = 0;
    uint8_t _injectGpTail = 0;
    uint8_t _injectGpCount = 0;
    uint8_t _injectHidHead = 0;
    uint8_t _injectHidTail = 0;
    uint8_t _injectHidCount = 0;

    // Current snapshots (for GET commands)
    Gamepad::GamepadState _currentGamepadState{};
    HIDReport _currentHIDReport{};

    // History ring buffer (depth 64)
    static constexpr uint8_t HISTORY_DEPTH = 64;
    GamepadStateEntry _gpHistory[HISTORY_DEPTH];
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
    void onGamepadState(Gamepad::GamepadState &) {}
    void onHIDReport(HIDReport &) {}
};

#endif

} // namespace ThetaGP::Test
