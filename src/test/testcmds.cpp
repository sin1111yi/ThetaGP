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

#include "test/testcmds.h"
#include "test/testinjector.h"
#include "test/framelayer.h"

#include "gamepad/gamepadstate.h"

#include "utils/log/log.h"

#include <cstring>

namespace ThetaGP::Test {

#ifdef THETAGP_ENABLE_TEST_API

TestCmdHandler &TestCmdHandler::getInstance() {
    static TestCmdHandler instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Helper: serialize a GamepadRawInput into a JsonObject
// ---------------------------------------------------------------------------
static void serializeGamepadRawInput(JsonObject obj,
                                  const Gamepad::GamepadRawInput &state) {
    obj["buttons"] = state.buttons;
    obj["dpad"] = state.dpad;
    obj["dpad_original"] = state.dpadOriginal;
    obj["aux"] = state.aux;
    obj["lx"] = state.lx;
    obj["ly"] = state.ly;
    obj["rx"] = state.rx;
    obj["ry"] = state.ry;
    obj["lt"] = state.lt;
    obj["rt"] = state.rt;
}

// ---------------------------------------------------------------------------
// Helper: serialize a HIDReport into a JsonObject
// ---------------------------------------------------------------------------
static void serializeHIDReport(JsonObject obj, const HIDReport &report) {
    obj["buttons"] = report.buttons;
    obj["dpad"] = report.direction;
    obj["l_x_axis"] = report.l_x_axis;
    obj["l_y_axis"] = report.l_y_axis;
    obj["r_x_axis"] = report.r_x_axis;
    obj["r_y_axis"] = report.r_y_axis;
}

// ---------------------------------------------------------------------------
// Command: test.inject_gamepad_state
// ---------------------------------------------------------------------------
static void handleInjectGamepadRawInput(const char *cmd, JsonDocument &doc,
                                     int queued) {
    TestInjector &ti = TestInjector::getInstance();

    Gamepad::GamepadRawInput state;
    state.buttons = doc["buttons"] | 0;
    state.dpad = doc["dpad"] | 0;
    state.dpadOriginal = doc["dpad_original"] | 0;
    state.aux = doc["aux"] | 0;
    state.lx = doc["lx"] | GAMEPAD_JOYSTICK_MID;
    state.ly = doc["ly"] | GAMEPAD_JOYSTICK_MID;
    state.rx = doc["rx"] | GAMEPAD_JOYSTICK_MID;
    state.ry = doc["ry"] | GAMEPAD_JOYSTICK_MID;
    state.lt = doc["lt"] | 0;
    state.rt = doc["rt"] | 0;

    JsonDocument resp;
    resp["status"] = "ok";
    resp["cmd"] = cmd;
    resp["queued"] = queued + 1;

    if (ti.injectGamepadRawInput(state)) {
        LOG_DEBUG("TestCmdHandler: gamepad state injected");
    } else {
        resp["status"] = "error";
        resp["error_code"] = 5;
        resp["reason"] = "inject queue full";
        LOG_WARN("TestCmdHandler: gamepad state inject queue full");
    }
    FrameLayer::getInstance().sendResponse(resp);
}

// ---------------------------------------------------------------------------
// Command: test.inject_hid_report
// ---------------------------------------------------------------------------
static void handleInjectHIDReport(const char *cmd, JsonDocument &doc,
                                  int queued) {
    TestInjector &ti = TestInjector::getInstance();

    HIDReport report;
    report.buttons = doc["buttons"] | 0;
    report.direction = doc["dpad"] | HID_HAT_NOTHING;
    report.dummy = 0;
    report.l_x_axis = doc["l_x_axis"] | HID_JOYSTICK_MID;
    report.l_y_axis = doc["l_y_axis"] | HID_JOYSTICK_MID;
    report.r_x_axis = doc["r_x_axis"] | HID_JOYSTICK_MID;
    report.r_y_axis = doc["r_y_axis"] | HID_JOYSTICK_MID;

    JsonDocument resp;
    resp["status"] = "ok";
    resp["cmd"] = cmd;
    resp["queued"] = queued + 1;

    if (ti.injectHIDReport(report)) {
        LOG_DEBUG("TestCmdHandler: HID report injected");
    } else {
        resp["status"] = "error";
        resp["error_code"] = 5;
        resp["reason"] = "inject queue full";
        LOG_WARN("TestCmdHandler: HID report inject queue full");
    }
    FrameLayer::getInstance().sendResponse(resp);
}

// ---------------------------------------------------------------------------
// Command: test.get_gamepad_state
// ---------------------------------------------------------------------------
static void handleGetGamepadRawInput(const char *cmd, JsonDocument &doc,
                                  int queued) {
    (void)doc;
    LOG_DEBUG("TestCmdHandler: get_gamepad_state");
    const auto &state =
        TestInjector::getInstance().getCurrentGamepadRawInput();

    JsonDocument resp;
    auto obj = resp.to<JsonObject>();
    serializeGamepadRawInput(obj, state);
    resp["status"] = "ok";
    resp["cmd"] = cmd;
    resp["queued"] = queued + 1;
    FrameLayer::getInstance().sendResponse(resp);
}

// ---------------------------------------------------------------------------
// Command: test.get_hid_report
// ---------------------------------------------------------------------------
static void handleGetHIDReport(const char *cmd, JsonDocument &doc, int queued) {
    (void)doc;
    LOG_DEBUG("TestCmdHandler: get_hid_report");
    const auto &report =
        TestInjector::getInstance().getCurrentHIDReport();

    JsonDocument resp;
    auto obj = resp.to<JsonObject>();
    serializeHIDReport(obj, report);
    resp["status"] = "ok";
    resp["cmd"] = cmd;
    resp["queued"] = queued + 1;
    FrameLayer::getInstance().sendResponse(resp);
}

// ---------------------------------------------------------------------------
// Command: test.set_override
// ---------------------------------------------------------------------------
static void handleSetOverride(const char *cmd, JsonDocument &doc, int queued) {
    TestInjector &ti = TestInjector::getInstance();
    const char *point = doc["point"] | "";
    bool enabled = doc["enabled"] | false;

    if (strcmp(point, "gamepad_state") == 0) {
        ti.setOverrideGamepadRawInput(enabled);
    } else if (strcmp(point, "hid_report") == 0) {
        ti.setOverrideHIDReport(enabled);
    } else {
        JsonDocument resp;
        resp["status"] = "error";
        resp["cmd"] = cmd;
        resp["queued"] = queued + 1;
        resp["error_code"] = 3;
        resp["reason"] = "invalid point";
        LOG_WARN("TestCmdHandler: set_override invalid point='%s'", point);
        FrameLayer::getInstance().sendResponse(resp);
        return;
    }

    JsonDocument resp;
    resp["status"] = "ok";
    resp["cmd"] = cmd;
    resp["queued"] = queued + 1;
    resp["point"] = point;
    resp["enabled"] = enabled;
    FrameLayer::getInstance().sendResponse(resp);
    LOG_DEBUG("TestCmdHandler: override '%s' -> %s", point,
              enabled ? "ON" : "OFF");
}

// ---------------------------------------------------------------------------
// Command: test.clear_inject
// ---------------------------------------------------------------------------
static void handleClearInject(const char *cmd, JsonDocument &doc, int queued) {
    TestInjector &ti = TestInjector::getInstance();
    const char *point = doc["point"] | "all";

    uint8_t cleared = 0;
    if (strcmp(point, "all") == 0 || strcmp(point, "gamepad_state") == 0) {
        cleared = ti.gamepadRawInputQueueCount();
        ti.clearGamepadRawInputQueue();
    }
    if (strcmp(point, "all") == 0 || strcmp(point, "hid_report") == 0) {
        cleared += ti.hidInjectQueueCount();
        ti.clearHIDInjectQueue();
    }

    JsonDocument resp;
    resp["status"] = "ok";
    resp["cmd"] = cmd;
    resp["queued"] = queued + 1;
    resp["cleared"] = cleared;
    FrameLayer::getInstance().sendResponse(resp);
    LOG_DEBUG("TestCmdHandler: cleared inject queue for '%s'", point);
}

// ---------------------------------------------------------------------------
// Command: test.set_mode
// ---------------------------------------------------------------------------
static void handleSetMode(const char *cmd, JsonDocument &doc, int queued) {
    TestInjector &ti = TestInjector::getInstance();
    int modeVal = doc["mode"] | 0;

    if (modeVal < 0 || modeVal > 2) {
        JsonDocument resp;
        resp["status"] = "error";
        resp["cmd"] = cmd;
        resp["queued"] = queued + 1;
        resp["error_code"] = 3;
        resp["reason"] = "invalid mode (0=PASS_THRU, 1=INJECT, 2=RECORD)";
        LOG_WARN("TestCmdHandler: set_mode invalid mode=%d", modeVal);
        FrameLayer::getInstance().sendResponse(resp);
        return;
    }

    auto mode = static_cast<TestInjector::TestMode>(modeVal);
    ti.setMode(mode);

    JsonDocument resp;
    resp["status"] = "ok";
    resp["cmd"] = cmd;
    resp["queued"] = queued + 1;
    resp["mode"] = modeVal;
    resp["mode_name"] = TestInjector::modeName(mode);
    FrameLayer::getInstance().sendResponse(resp);
    LOG_INFO("TestCmdHandler: mode set to %s", TestInjector::modeName(mode));
}

// ---------------------------------------------------------------------------
// Command: test.get_mode
// ---------------------------------------------------------------------------
static void handleGetMode(const char *cmd, JsonDocument &doc, int queued) {
    (void)doc;
    LOG_DEBUG("TestCmdHandler: get_mode");
    auto mode = TestInjector::getInstance().getMode();

    JsonDocument resp;
    resp["status"] = "ok";
    resp["cmd"] = cmd;
    resp["queued"] = queued + 1;
    resp["mode"] = static_cast<int>(mode);
    resp["mode_name"] = TestInjector::modeName(mode);
    FrameLayer::getInstance().sendResponse(resp);
}

// ---------------------------------------------------------------------------
// Command: test.get_history
// ---------------------------------------------------------------------------
static void handleGetHistory(const char *cmd, JsonDocument &doc, int queued) {
    TestInjector &ti = TestInjector::getInstance();
    const char *type = doc["type"] | "";
    int count = doc["count"] | 5;
    LOG_DEBUG("TestCmdHandler: get_history type='%s' count=%d", type, count);
    if (count < 0) {
        count = 0;
    }
    if (count > 64) {
        count = 64;
    }

    JsonDocument resp;
    resp["status"] = "ok";
    resp["cmd"] = cmd;
    resp["queued"] = queued + 1;
    resp["type"] = type;

    if (strcmp(type, "gamepad_state") == 0) {
        TestInjector::GamepadRawInputEntry entries[64];
        uint8_t actual =
            ti.readGamepadHistory(entries, static_cast<uint8_t>(count));
        resp["count"] = actual;
        auto arr = resp["entries"].to<JsonArray>();
        for (uint8_t i = 0; i < actual; i++) {
            auto entry = arr.add<JsonObject>();
            entry["index"] = i;
            serializeGamepadRawInput(entry, entries[i].state);
        }
    } else if (strcmp(type, "hid_report") == 0) {
        TestInjector::HIDReportEntry entries[64];
        uint8_t actual =
            ti.readHIDHistory(entries, static_cast<uint8_t>(count));
        resp["count"] = actual;
        auto arr = resp["entries"].to<JsonArray>();
        for (uint8_t i = 0; i < actual; i++) {
            auto entry = arr.add<JsonObject>();
            entry["index"] = i;
            serializeHIDReport(entry, entries[i].report);
        }
    } else {
        resp["status"] = "error";
        resp["error_code"] = 3;
        resp["reason"] = "invalid type (gamepad_state or hid_report)";
        LOG_WARN("TestCmdHandler: get_history invalid type='%s'", type);
    }

    FrameLayer::getInstance().sendResponse(resp);
}

// ---------------------------------------------------------------------------
// Command: test.clear_history
// ---------------------------------------------------------------------------
static void handleClearHistory(const char *cmd, JsonDocument &doc, int queued) {
    TestInjector &ti = TestInjector::getInstance();
    const char *type = doc["type"] | "all";

    if (strcmp(type, "all") == 0 || strcmp(type, "gamepad_state") == 0) {
        ti.clearGamepadHistory();
    }
    if (strcmp(type, "all") == 0 || strcmp(type, "hid_report") == 0) {
        ti.clearHIDHistory();
    }

    JsonDocument resp;
    resp["status"] = "ok";
    resp["cmd"] = cmd;
    resp["queued"] = queued + 1;
    resp["type"] = type;
    FrameLayer::getInstance().sendResponse(resp);
    LOG_DEBUG("TestCmdHandler: cleared history for '%s'", type);
}

// ---------------------------------------------------------------------------
// Command: test.get_status
// ---------------------------------------------------------------------------
static void handleGetStatus(const char *cmd, JsonDocument &doc, int queued) {
    (void)doc;
    LOG_DEBUG("TestCmdHandler: get_status");
    TestInjector &ti = TestInjector::getInstance();
    auto mode = ti.getMode();

    JsonDocument resp;
    resp["status"] = "ok";
    resp["cmd"] = cmd;
    resp["queued"] = queued + 1;
    resp["mode"] = static_cast<int>(mode);
    resp["mode_name"] = TestInjector::modeName(mode);
    resp["override_gamepad_state"] = ti.isOverrideGamepadRawInput();
    resp["override_hid_report"] = ti.isOverrideHIDReport();
    resp["gamepad_history_count"] = ti.getGamepadHistoryCount();
    resp["hid_history_count"] = ti.getHIDHistoryCount();
    resp["gamepad_inject_queued"] = ti.gamepadRawInputQueueCount();
    resp["hid_inject_queued"] = ti.hidInjectQueueCount();

    // Add current snapshots
    auto gpState = resp["gamepad_state"].to<JsonObject>();
    serializeGamepadRawInput(gpState, ti.getCurrentGamepadRawInput());

    auto hidReport = resp["hid_report"].to<JsonObject>();
    serializeHIDReport(hidReport, ti.getCurrentHIDReport());

    FrameLayer::getInstance().sendResponse(resp);
}

// ---------------------------------------------------------------------------
// Command: test.reset
// ---------------------------------------------------------------------------
static void handleReset(const char *cmd, JsonDocument &doc, int queued) {
    (void)doc;
    TestInjector::getInstance().reset();

    JsonDocument resp;
    resp["status"] = "ok";
    resp["cmd"] = cmd;
    resp["queued"] = queued + 1;
    FrameLayer::getInstance().sendResponse(resp);
    LOG_INFO("TestCmdHandler: TestInjector reset");
}

// ---------------------------------------------------------------------------
// Main dispatch
// ---------------------------------------------------------------------------
void TestCmdHandler::handle(const char *cmd, JsonDocument &doc) {
    int queued = doc["queued"].as<int>();
    LOG_DEBUG("TestCmdHandler: cmd='%s' queued=%d", cmd, queued);

    if (strcmp(cmd, "test.inject_gamepad_state") == 0) {
        handleInjectGamepadRawInput(cmd, doc, queued);
    } else if (strcmp(cmd, "test.inject_hid_report") == 0) {
        handleInjectHIDReport(cmd, doc, queued);
    } else if (strcmp(cmd, "test.get_gamepad_state") == 0) {
        handleGetGamepadRawInput(cmd, doc, queued);
    } else if (strcmp(cmd, "test.get_hid_report") == 0) {
        handleGetHIDReport(cmd, doc, queued);
    } else if (strcmp(cmd, "test.set_override") == 0) {
        handleSetOverride(cmd, doc, queued);
    } else if (strcmp(cmd, "test.clear_inject") == 0) {
        handleClearInject(cmd, doc, queued);
    } else if (strcmp(cmd, "test.set_mode") == 0) {
        handleSetMode(cmd, doc, queued);
    } else if (strcmp(cmd, "test.get_mode") == 0) {
        handleGetMode(cmd, doc, queued);
    } else if (strcmp(cmd, "test.get_history") == 0) {
        handleGetHistory(cmd, doc, queued);
    } else if (strcmp(cmd, "test.clear_history") == 0) {
        handleClearHistory(cmd, doc, queued);
    } else if (strcmp(cmd, "test.get_status") == 0) {
        handleGetStatus(cmd, doc, queued);
    } else if (strcmp(cmd, "test.reset") == 0) {
        handleReset(cmd, doc, queued);
    } else {
        LOG_WARN("TestCmdHandler: unknown test command: %s", cmd);
        JsonDocument resp;
        resp["status"] = "error";
        resp["cmd"] = cmd;
        resp["queued"] = queued + 1;
        resp["error_code"] = 1;
        resp["reason"] = "unknown command";
        FrameLayer::getInstance().sendResponse(resp);
    }
}

#endif // THETAGP_ENABLE_TEST_API

} // namespace ThetaGP::Test