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

#include <ArduinoJson.h>

namespace ThetaGP::Test {

#ifdef THETAGP_ENABLE_TEST_API

/**
 * TestCmdHandler — handles commands in the `test.` domain.
 *
 * Supported commands (12 total):
 *   test.inject_gamepad_state  - Inject a GamepadRawInput into the pipeline
 *   test.inject_hid_report     - Inject a HIDReport into the pipeline
 *   test.get_gamepad_state     - Read current gamepad state snapshot
 *   test.get_hid_report        - Read current HID report snapshot
 *   test.set_override          - Enable/disable persistent override
 *   test.clear_inject          - Clear inject queue(s)
 *   test.set_mode              - Set TestMode (0=PASS_THRU, 1=INJECT, 2=RECORD)
 *   test.get_mode              - Read current TestMode
 *   test.get_history           - Read history ring buffer entries
 *   test.clear_history         - Clear history ring buffer(s)
 *   test.get_status            - Read full status (mode, queue counts, etc.)
 *   test.reset                 - Reset TestInjector to defaults
 */
class TestCmdHandler {
public:
    TestCmdHandler() = default;
    TestCmdHandler(const TestCmdHandler &) = delete;
    TestCmdHandler &operator=(const TestCmdHandler &) = delete;

    static TestCmdHandler &getInstance();
    static void handle(const char *cmd, JsonDocument &doc);
    static void registerHandlers();
};

#else

// Production mode no-op stub
class TestCmdHandler {
public:
    static TestCmdHandler &getInstance() { static TestCmdHandler i; return i; }
    static void handle(const char *, JsonDocument &) {}
    static void registerHandlers() {}
};

#endif

} // namespace ThetaGP::Test
