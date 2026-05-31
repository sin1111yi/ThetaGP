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
#include "test/dispatcher.h"
#include "test/testinjector.h"
#include "test/framelayer.h"

#include "gamepad/gamepadstate.h"

#include "utils/log/log.h"

#include "protocol/proto.h"

#include <cstring>

namespace ThetaGP::Test {

#ifdef THETAGP_ENABLE_TEST_API

TestCmdHandler &TestCmdHandler::getInstance() {
    static TestCmdHandler instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Handler functions (CommandHandler signature)
// ---------------------------------------------------------------------------

static void handleInjectGamepadRawInput(const char *cmd, JsonDocument &doc) {
    Gamepad::GamepadRawInput state;
    Proto::deserializeGamepadRawInput(doc, state);
    auto &inj = TestInjector::getInstance();
    bool ok = inj.injectGamepadRawInput(state);
    JsonDocument resp;
    resp["status"] = ok ? "ok" : "error";
    resp["cmd"] = cmd;
    int queued = doc["queued"].as<int>();
    resp["queued"] = queued + 1;
    if (ok) {
        resp["queued"] = inj.gamepadRawInputQueueCount();
    } else {
        resp["error_code"] = static_cast<uint8_t>(Proto::ErrorCode::ERR_QUEUE_FULL);
        resp["reason"] = "Inject queue is full";
    }
    FrameLayer::getInstance().sendResponse(resp);
}

static void handleInjectHIDReport(const char *cmd, JsonDocument &doc) {
    HIDReport report{};
    Proto::deserializeHIDReport(doc, report);
    auto &inj = TestInjector::getInstance();
    bool ok = inj.injectHIDReport(report);
    JsonDocument resp;
    resp["status"] = ok ? "ok" : "error";
    resp["cmd"] = cmd;
    int queued = doc["queued"].as<int>();
    resp["queued"] = queued + 1;
    if (ok) {
        resp["queued"] = inj.hidInjectQueueCount();
    } else {
        resp["error_code"] = static_cast<uint8_t>(Proto::ErrorCode::ERR_QUEUE_FULL);
        resp["reason"] = "Inject queue is full";
    }
    FrameLayer::getInstance().sendResponse(resp);
}

static void handleGetGamepadRawInput(const char *cmd, JsonDocument &doc) {
    auto &inj = TestInjector::getInstance();
    const auto &state = inj.getCurrentGamepadRawInput();
    JsonDocument resp;
    auto obj = resp.to<JsonObject>();
    Proto::serializeGamepadRawInput(obj, state);
    obj["status"] = "ok";
    obj["cmd"] = cmd;
    int queued = doc["queued"].as<int>();
    obj["queued"] = queued + 1;
    FrameLayer::getInstance().sendResponse(resp);
}

static void handleGetHIDReport(const char *cmd, JsonDocument &doc) {
    auto &inj = TestInjector::getInstance();
    const auto &report = inj.getCurrentHIDReport();
    JsonDocument resp;
    auto obj = resp.to<JsonObject>();
    Proto::serializeHIDReport(obj, report);
    obj["status"] = "ok";
    obj["cmd"] = cmd;
    int queued = doc["queued"].as<int>();
    obj["queued"] = queued + 1;
    FrameLayer::getInstance().sendResponse(resp);
}

static void handleSetOverride(const char *cmd, JsonDocument &doc) {
    const char *point = doc["point"] | "";
    bool enabled = doc["enabled"] | false;
    auto &inj = TestInjector::getInstance();
    if (strcmp(point, "gamepad_state") == 0) {
        inj.setOverrideGamepadRawInput(enabled);
    } else if (strcmp(point, "hid_report") == 0) {
        inj.setOverrideHIDReport(enabled);
    } else {
        JsonDocument resp;
        resp["status"] = "error";
        resp["cmd"] = cmd;
        int queued = doc["queued"].as<int>();
        resp["queued"] = queued + 1;
        resp["error_code"] = static_cast<uint8_t>(Proto::ErrorCode::ERR_INTERNAL);
        resp["reason"] = "Invalid point value";
        FrameLayer::getInstance().sendResponse(resp);
        return;
    }
    JsonDocument resp;
    resp["status"] = "ok";
    resp["cmd"] = cmd;
    int queued = doc["queued"].as<int>();
    resp["queued"] = queued + 1;
    resp["point"] = point;
    resp["enabled"] = enabled;
    FrameLayer::getInstance().sendResponse(resp);
}

static void handleClearInject(const char *cmd, JsonDocument &doc) {
    const char *point = doc["point"] | "all";
    auto &inj = TestInjector::getInstance();
    uint8_t cleared = 0;
    if (strcmp(point, "gamepad_state") == 0 || strcmp(point, "all") == 0) {
        cleared += inj.gamepadRawInputQueueCount();
        inj.clearGamepadRawInputQueue();
    }
    if (strcmp(point, "hid_report") == 0 || strcmp(point, "all") == 0) {
        cleared += inj.hidInjectQueueCount();
        inj.clearHIDInjectQueue();
    }
    JsonDocument resp;
    resp["status"] = "ok";
    resp["cmd"] = cmd;
    int queued = doc["queued"].as<int>();
    resp["queued"] = queued + 1;
    resp["cleared"] = cleared;
    FrameLayer::getInstance().sendResponse(resp);
}

static void handleSetMode(const char *cmd, JsonDocument &doc) {
    uint8_t modeVal = doc["mode"] | 0;
    auto &inj = TestInjector::getInstance();
    if (modeVal > 2) {
        JsonDocument resp;
        resp["status"] = "error";
        resp["cmd"] = cmd;
        int queued = doc["queued"].as<int>();
        resp["queued"] = queued + 1;
        resp["error_code"] = static_cast<uint8_t>(Proto::ErrorCode::ERR_INTERNAL);
        resp["reason"] = "Invalid mode value";
        FrameLayer::getInstance().sendResponse(resp);
        return;
    }
    inj.setMode(static_cast<TestInjector::TestMode>(modeVal));
    JsonDocument resp;
    resp["status"] = "ok";
    resp["cmd"] = cmd;
    int queued = doc["queued"].as<int>();
    resp["queued"] = queued + 1;
    resp["mode"] = modeVal;
    resp["mode_name"] = TestInjector::modeName(inj.getMode());
    FrameLayer::getInstance().sendResponse(resp);
}

static void handleGetMode(const char *cmd, JsonDocument &doc) {
    auto &inj = TestInjector::getInstance();
    JsonDocument resp;
    resp["status"] = "ok";
    resp["cmd"] = cmd;
    int queued = doc["queued"].as<int>();
    resp["queued"] = queued + 1;
    resp["mode"] = static_cast<uint8_t>(inj.getMode());
    resp["mode_name"] = TestInjector::modeName(inj.getMode());
    FrameLayer::getInstance().sendResponse(resp);
}

static void handleGetHistory(const char *cmd, JsonDocument &doc) {
    const char *historyType = doc["type"] | "gamepad_state";
    uint8_t count = doc["count"] | 10;
    if (count > 64) {
        count = 64;
    }
    auto &inj = TestInjector::getInstance();
    JsonDocument resp;
    resp["status"] = "ok";
    resp["cmd"] = cmd;
    int queued = doc["queued"].as<int>();
    resp["queued"] = queued + 1;
    resp["type"] = historyType;
    if (strcmp(historyType, "gamepad_state") == 0) {
        // Read from TestInjector's internal history and convert to protocol format
        TestInjector::GamepadRawInputEntry rawEntries[64];
        uint8_t actual = inj.readGamepadHistory(rawEntries, count);
        resp["count"] = actual;
        JsonArray entries = resp["entries"].to<JsonArray>();
        for (uint8_t i = 0; i < actual; ++i) {
            ThetaGP::Test::GamepadRawInputEntry protoEntry;
            protoEntry.index = i;
            protoEntry.timestampUs = rawEntries[i].timestampUs;
            protoEntry.buttons = rawEntries[i].state.buttons;
            protoEntry.dpad = rawEntries[i].state.dpad;
            protoEntry.lx = rawEntries[i].state.lx;
            protoEntry.ly = rawEntries[i].state.ly;
            protoEntry.rx = rawEntries[i].state.rx;
            protoEntry.ry = rawEntries[i].state.ry;
            protoEntry.lt = rawEntries[i].state.lt;
            protoEntry.rt = rawEntries[i].state.rt;
            JsonObject entryObj = entries.add<JsonObject>();
            Proto::serializeGamepadRawInputEntry(entryObj, protoEntry);
        }
    } else if (strcmp(historyType, "hid_report") == 0) {
        TestInjector::HIDReportEntry rawEntries[64];
        uint8_t actual = inj.readHIDHistory(rawEntries, count);
        resp["count"] = actual;
        JsonArray entries = resp["entries"].to<JsonArray>();
        for (uint8_t i = 0; i < actual; ++i) {
            ThetaGP::Test::HIDReportEntry protoEntry;
            protoEntry.index = i;
            protoEntry.timestampUs = rawEntries[i].timestampUs;
            protoEntry.buttons = rawEntries[i].report.buttons;
            protoEntry.direction = rawEntries[i].report.direction;
            protoEntry.l_x_axis = rawEntries[i].report.l_x_axis;
            protoEntry.l_y_axis = rawEntries[i].report.l_y_axis;
            protoEntry.r_x_axis = rawEntries[i].report.r_x_axis;
            protoEntry.r_y_axis = rawEntries[i].report.r_y_axis;
            JsonObject entryObj = entries.add<JsonObject>();
            Proto::serializeHIDReportEntry(entryObj, protoEntry);
        }
    } else {
        resp["status"] = "error";
        resp["error_code"] = static_cast<uint8_t>(Proto::ErrorCode::ERR_INTERNAL);
        resp["reason"] = "Invalid history type";
    }
    FrameLayer::getInstance().sendResponse(resp);
}

static void handleClearHistory(const char *cmd, JsonDocument &doc) {
    const char *historyType = doc["type"] | "gamepad_state";
    auto &inj = TestInjector::getInstance();
    if (strcmp(historyType, "gamepad_state") == 0 || strcmp(historyType, "all") == 0) {
        inj.clearGamepadHistory();
    }
    if (strcmp(historyType, "hid_report") == 0 || strcmp(historyType, "all") == 0) {
        inj.clearHIDHistory();
    }
    JsonDocument resp;
    resp["status"] = "ok";
    resp["cmd"] = cmd;
    int queued = doc["queued"].as<int>();
    resp["queued"] = queued + 1;
    resp["type"] = historyType;
    FrameLayer::getInstance().sendResponse(resp);
}

static void handleGetStatus(const char *cmd, JsonDocument &doc) {
    auto &inj = TestInjector::getInstance();
    ThetaGP::Test::TestStatus status;
    status.mode = static_cast<uint8_t>(inj.getMode());
    status.mode_name = TestInjector::modeName(inj.getMode());
    status.override_gamepad_state = inj.isOverrideGamepadRawInput();
    status.override_hid_report = inj.isOverrideHIDReport();
    status.gamepad_history_count = inj.getGamepadHistoryCount();
    status.hid_history_count = inj.getHIDHistoryCount();
    status.gamepad_inject_queued = inj.gamepadRawInputQueueCount();
    status.hid_inject_queued = inj.hidInjectQueueCount();
    JsonDocument resp;
    auto obj = resp.to<JsonObject>();
    Proto::serializeTestStatus(obj, status);
    obj["status"] = "ok";
    obj["cmd"] = cmd;
    int queued = doc["queued"].as<int>();
    obj["queued"] = queued + 1;
    FrameLayer::getInstance().sendResponse(resp);
}

static void handleReset(const char *cmd, JsonDocument &doc) {
    auto &inj = TestInjector::getInstance();
    inj.reset();
    JsonDocument resp;
    resp["status"] = "ok";
    resp["cmd"] = cmd;
    int queued = doc["queued"].as<int>();
    resp["queued"] = queued + 1;
    FrameLayer::getInstance().sendResponse(resp);
}

// ---------------------------------------------------------------------------
// registerHandlers — self-register with the Dispatcher and Proto
// ---------------------------------------------------------------------------
void TestCmdHandler::registerHandlers() {
    Dispatcher::getInstance().registerHandler("test", TestCmdHandler::handle);
    Proto::registerTestInjectGamepadState(handleInjectGamepadRawInput);
    Proto::registerTestInjectHidReport(handleInjectHIDReport);
    Proto::registerTestGetGamepadState(handleGetGamepadRawInput);
    Proto::registerTestGetHidReport(handleGetHIDReport);
    Proto::registerTestSetOverride(handleSetOverride);
    Proto::registerTestClearInject(handleClearInject);
    Proto::registerTestSetMode(handleSetMode);
    Proto::registerTestGetMode(handleGetMode);
    Proto::registerTestGetHistory(handleGetHistory);
    Proto::registerTestClearHistory(handleClearHistory);
    Proto::registerTestGetStatus(handleGetStatus);
    Proto::registerTestReset(handleReset);
}

// ---------------------------------------------------------------------------
// Main dispatch — delegates to Proto-generated dispatch table
// ---------------------------------------------------------------------------
void TestCmdHandler::handle(const char *cmd, JsonDocument &doc) {
    int queued = doc["queued"].as<int>();
    LOG_DEBUG("TestCmdHandler: cmd='%s' queued=%d", cmd, queued);
    Proto::dispatch(cmd, doc);
}

#else

// All methods are inlined in testcmds.h for production mode
// (empty class stub with no-op implementations)

#endif // THETAGP_ENABLE_TEST_API

} // namespace ThetaGP::Test
