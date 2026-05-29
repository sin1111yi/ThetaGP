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

#include "test/testinjector.h"

#include "drivers/peripherals/systick.h"
#include "utils/log/log.h"

#include <cstring>

namespace ThetaGP::Test {

#ifdef THETAGP_ENABLE_TEST_API

TestInjector &TestInjector::getInstance() {
    static TestInjector instance;
    return instance;
}

const char *TestInjector::modeName(TestMode mode) {
    switch (mode) {
    case TestMode::PASS_THRU:
        return "PASS_THRU";
    case TestMode::INJECT:
        return "INJECT";
    case TestMode::RECORD:
        return "RECORD";
    default:
        return "UNKNOWN";
    }
}

void TestInjector::setMode(TestMode mode) {
    LOG_DEBUG("TestInjector: mode -> %s", modeName(mode));
    _mode = mode;
}

// --- Inject queue helpers ---

bool TestInjector::injectGamepadState(const Gamepad::GamepadState &state) {
    if (_injectGpCount >= INJECT_QUEUE_DEPTH) {
        LOG_WARN("TestInjector: gamepad state inject queue full");
        return false;
    }
    _injectedGamepadStates[_injectGpHead] = state;
    _injectGpHead = (_injectGpHead + 1) % INJECT_QUEUE_DEPTH;
    _injectGpCount++;
    LOG_DEBUG("TestInjector: gamepad state injected (queue=%u)", _injectGpCount);
    return true;
}

bool TestInjector::injectHIDReport(const HIDReport &report) {
    if (_injectHidCount >= INJECT_QUEUE_DEPTH) {
        LOG_WARN("TestInjector: HID report inject queue full");
        return false;
    }
    _injectedHIDReports[_injectHidHead] = report;
    _injectHidHead = (_injectHidHead + 1) % INJECT_QUEUE_DEPTH;
    _injectHidCount++;
    LOG_DEBUG("TestInjector: HID report injected (queue=%u)", _injectHidCount);
    return true;
}

void TestInjector::clearGamepadInjectQueue() {
    _injectGpHead = 0;
    _injectGpTail = 0;
    _injectGpCount = 0;
    LOG_DEBUG("TestInjector: gamepad inject queue cleared");
}

void TestInjector::clearHIDInjectQueue() {
    _injectHidHead = 0;
    _injectHidTail = 0;
    _injectHidCount = 0;
    LOG_DEBUG("TestInjector: HID inject queue cleared");
}

// --- Override control ---

void TestInjector::setOverrideGamepadState(bool enabled) {
    LOG_DEBUG("TestInjector: override gamepad_state -> %s", enabled ? "ON" : "OFF");
    _overrideGamepadState = enabled;
}

void TestInjector::setOverrideHIDReport(bool enabled) {
    LOG_DEBUG("TestInjector: override HID report -> %s", enabled ? "ON" : "OFF");
    _overrideHIDReport = enabled;
}

// --- History recording ---

void TestInjector::recordGamepadState(const Gamepad::GamepadState &state) {
    if (_gpHistoryCount < HISTORY_DEPTH) {
        _gpHistoryCount++;
    }
    _gpHistory[_gpHistoryHead].timestampUs = micros();
    _gpHistory[_gpHistoryHead].state = state;
    _gpHistoryHead = (_gpHistoryHead + 1) % HISTORY_DEPTH;
}

void TestInjector::recordHIDReport(const HIDReport &report) {
    if (_hidHistoryCount < HISTORY_DEPTH) {
        _hidHistoryCount++;
    }
    _hidHistory[_hidHistoryHead].timestampUs = micros();
    _hidHistory[_hidHistoryHead].report = report;
    _hidHistoryHead = (_hidHistoryHead + 1) % HISTORY_DEPTH;
}

uint8_t TestInjector::readGamepadHistory(GamepadStateEntry *out,
                                         uint8_t maxCount) const {
    uint8_t count = _gpHistoryCount < maxCount ? _gpHistoryCount : maxCount;
    // Walk backwards from head to get newest entries
    int32_t idx =
        static_cast<int32_t>(_gpHistoryHead) - 1;
    for (uint8_t i = 0; i < count; i++) {
        if (idx < 0) {
            idx = HISTORY_DEPTH - 1;
        }
        out[i] = _gpHistory[idx];
        idx--;
    }
    return count;
}

uint8_t TestInjector::readHIDHistory(HIDReportEntry *out,
                                     uint8_t maxCount) const {
    uint8_t count = _hidHistoryCount < maxCount ? _hidHistoryCount : maxCount;
    int32_t idx = static_cast<int32_t>(_hidHistoryHead) - 1;
    for (uint8_t i = 0; i < count; i++) {
        if (idx < 0) {
            idx = HISTORY_DEPTH - 1;
        }
        out[i] = _hidHistory[idx];
        idx--;
    }
    return count;
}

void TestInjector::clearGamepadHistory() {
    _gpHistoryHead = 0;
    _gpHistoryCount = 0;
    LOG_DEBUG("TestInjector: gamepad history cleared");
}

void TestInjector::clearHIDHistory() {
    _hidHistoryHead = 0;
    _hidHistoryCount = 0;
    LOG_DEBUG("TestInjector: HID history cleared");
}

// --- Full reset ---

void TestInjector::reset() {
    _mode = TestMode::PASS_THRU;
    _overrideGamepadState = false;
    _overrideHIDReport = false;

    clearGamepadInjectQueue();
    clearHIDInjectQueue();
    clearGamepadHistory();
    clearHIDHistory();

    _currentGamepadState = Gamepad::GamepadState{};
    _currentHIDReport = HIDReport{};

    LOG_DEBUG("TestInjector: fully reset");
}

// --- Point A hook ---

void TestInjector::onGamepadState(Gamepad::GamepadState &state) {
    // 1. Save snapshot
    _currentGamepadState = state;

    // 2. Record history in RECORD mode, or in PASS_THRU if there is room
    if (_mode == TestMode::RECORD) {
        recordGamepadState(state);
    } else if (_mode == TestMode::PASS_THRU && _gpHistoryCount < HISTORY_DEPTH) {
        recordGamepadState(state);
    }

    // 3. INJECT mode: replace state from queue
    if (_mode == TestMode::INJECT && _injectGpCount > 0) {
        state = _injectedGamepadStates[_injectGpTail];
        _injectGpTail = (_injectGpTail + 1) % INJECT_QUEUE_DEPTH;
        _injectGpCount--;
        LOG_DEBUG("TestInjector: injected gamepad state (queue=%u)",
                  _injectGpCount);
    }

    // 4. Override mode: replace with last injected value if queue non-empty
    if (_overrideGamepadState) {
        if (_injectGpCount > 0) {
            // Use the most recently injected value
            uint8_t lastIdx =
                (_injectGpHead == 0) ? INJECT_QUEUE_DEPTH - 1 : _injectGpHead - 1;
            state = _injectedGamepadStates[lastIdx];
        }
    }
}

// --- Point B hook ---

void TestInjector::onHIDReport(HIDReport &report) {
    // 1. Save snapshot
    _currentHIDReport = report;

    // 2. Record history in RECORD mode, or in PASS_THRU if there is room
    if (_mode == TestMode::RECORD) {
        recordHIDReport(report);
    } else if (_mode == TestMode::PASS_THRU && _hidHistoryCount < HISTORY_DEPTH) {
        recordHIDReport(report);
    }

    // 3. INJECT mode: replace report from queue
    if (_mode == TestMode::INJECT && _injectHidCount > 0) {
        report = _injectedHIDReports[_injectHidTail];
        _injectHidTail = (_injectHidTail + 1) % INJECT_QUEUE_DEPTH;
        _injectHidCount--;
        LOG_DEBUG("TestInjector: injected HID report (queue=%u)",
                  _injectHidCount);
    }

    // 4. Override mode: replace with last injected value if queue non-empty
    if (_overrideHIDReport) {
        if (_injectHidCount > 0) {
            uint8_t lastIdx =
                (_injectHidHead == 0) ? INJECT_QUEUE_DEPTH - 1 : _injectHidHead - 1;
            report = _injectedHIDReports[lastIdx];
        }
    }
}

#endif // THETAGP_ENABLE_TEST_API

} // namespace ThetaGP::Test
