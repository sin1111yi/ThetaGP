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

#include "drivers/device/flash/flash_w25qxx.h"
#include "drivers/device/devmem.h"

#include "gamepad/gamepadstate.h"

#include "utils/log/log.h"
#include "utils/mempool/mempoolmanager.h"

#include "protocol/proto.h"

#include <cstring>

namespace ThetaGP::Test {

#ifdef THETAGP_CFG_TEST

static COMMON_ZERO_INIT char s_testRespBuf[4096];

TestCmdHandler &TestCmdHandler::getInstance() {
    static TestCmdHandler instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Handler functions (CommandHandler signature)
// ---------------------------------------------------------------------------

static void handleInjectGamepadRawInput(const char *cmd, const Json &json) {
    // Build GamepadRawInput from json fields
    Gamepad::GamepadRawInput state;
    state.buttons = static_cast<uint32_t>(json.getInt("buttons", 0));
    state.dpad = static_cast<uint8_t>(json.getInt("dpad", 0));
    state.dpadOriginal = static_cast<uint8_t>(json.getInt("dpad_original", 0));
    state.lx = static_cast<uint16_t>(json.getInt("lx", GAMEPAD_JOYSTICK_MID));
    state.ly = static_cast<uint16_t>(json.getInt("ly", GAMEPAD_JOYSTICK_MID));
    state.rx = static_cast<uint16_t>(json.getInt("rx", GAMEPAD_JOYSTICK_MID));
    state.ry = static_cast<uint16_t>(json.getInt("ry", GAMEPAD_JOYSTICK_MID));
    state.lt = static_cast<uint8_t>(json.getInt("lt", 0));
    state.rt = static_cast<uint8_t>(json.getInt("rt", 0));

    auto &inj = TestInjector::getInstance();
    bool ok = inj.injectGamepadRawInput(state);
    int queued = json.getInt("queued");

    Json resp;
    resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
    if (ok) {
        resp.printf("{status:%Q,cmd:%Q,queued:%d}",
                    "ok", cmd, inj.gamepadRawInputQueueCount());
    } else {
        resp.printf("{status:%Q,cmd:%Q,queued:%d,error_code:%d,reason:%Q}",
                    "error", cmd, queued + 1,
                    static_cast<int>(Proto::ErrorCode::ERR_QUEUE_FULL),
                    "Inject queue is full");
    }
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

static void handleInjectHIDReport(const char *cmd, const Json &json) {
    HIDReport report{};
    report.buttons = static_cast<uint32_t>(json.getInt("buttons", 0));
    report.direction = static_cast<uint8_t>(json.getInt("dpad", 8));
    report.l_x_axis = static_cast<uint8_t>(json.getInt("l_x_axis", 128));
    report.l_y_axis = static_cast<uint8_t>(json.getInt("l_y_axis", 128));
    report.r_x_axis = static_cast<uint8_t>(json.getInt("r_x_axis", 128));
    report.r_y_axis = static_cast<uint8_t>(json.getInt("r_y_axis", 128));

    auto &inj = TestInjector::getInstance();
    bool ok = inj.injectHIDReport(report);
    int queued = json.getInt("queued");

    Json resp;
    resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
    if (ok) {
        resp.printf("{status:%Q,cmd:%Q,queued:%d}",
                    "ok", cmd, inj.hidInjectQueueCount());
    } else {
        resp.printf("{status:%Q,cmd:%Q,queued:%d,error_code:%d,reason:%Q}",
                    "error", cmd, queued + 1,
                    static_cast<int>(Proto::ErrorCode::ERR_QUEUE_FULL),
                    "Inject queue is full");
    }
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

static void handleGetGamepadRawInput(const char *cmd, const Json &json) {
    auto &inj = TestInjector::getInstance();
    const auto &state = inj.getCurrentGamepadRawInput();
    int queued = json.getInt("queued");

    Json resp;
    resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
    resp.printf("{status:%Q,cmd:%Q,queued:%d,buttons:%lu,dpad:%d,"
                "lx:%d,ly:%d,rx:%d,ry:%d,lt:%d,rt:%d}",
                "ok", cmd, queued + 1,
                (unsigned long)state.buttons, state.dpad,
                state.lx, state.ly, state.rx, state.ry,
                state.lt, state.rt);
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

static void handleGetHIDReport(const char *cmd, const Json &json) {
    auto &inj = TestInjector::getInstance();
    const auto &report = inj.getCurrentHIDReport();
    int queued = json.getInt("queued");

    Json resp;
    resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
    resp.printf("{status:%Q,cmd:%Q,queued:%d,buttons:%lu,dpad:%d,"
                "l_x_axis:%d,l_y_axis:%d,r_x_axis:%d,r_y_axis:%d}",
                "ok", cmd, queued + 1,
                (unsigned long)report.buttons, report.direction,
                report.l_x_axis, report.l_y_axis,
                report.r_x_axis, report.r_y_axis);
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

static void handleSetOverride(const char *cmd, const Json &json) {
    char pointBuf[32];
    const char *point = json.getStrCopy("point", pointBuf, sizeof(pointBuf));
    bool enabled = json.getBool("enabled", false);
    auto &inj = TestInjector::getInstance();
    int queued = json.getInt("queued");

    if (!point) {
        Json resp;
        resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
        resp.printf("{status:%Q,cmd:%Q,queued:%d,error_code:%d,reason:%Q}",
                    "error", cmd, queued + 1,
                    static_cast<int>(Proto::ErrorCode::ERR_INTERNAL),
                    "Missing point field");
        uint16_t len = resp.end();
        FrameLayer::getInstance().sendResponse(resp.c_str(), len);
        return;
    }

    if (strcmp(point, "gamepad_state") == 0) {
        inj.setOverrideGamepadRawInput(enabled);
    } else if (strcmp(point, "hid_report") == 0) {
        inj.setOverrideHIDReport(enabled);
    } else {
        Json resp;
        resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
        resp.printf("{status:%Q,cmd:%Q,queued:%d,error_code:%d,reason:%Q}",
                    "error", cmd, queued + 1,
                    static_cast<int>(Proto::ErrorCode::ERR_INTERNAL),
                    "Invalid point value");
        uint16_t len = resp.end();
        FrameLayer::getInstance().sendResponse(resp.c_str(), len);
        return;
    }

    Json resp;
    resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
    resp.printf("{status:%Q,cmd:%Q,queued:%d,point:%Q,enabled:%B}",
                "ok", cmd, queued + 1, point, enabled);
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

static void handleClearInject(const char *cmd, const Json &json) {
    char pointBuf[32];
    const char *point = json.getStrCopy("point", pointBuf, sizeof(pointBuf));
    if (!point) point = "all";
    auto &inj = TestInjector::getInstance();
    uint8_t cleared = 0;
    int queued = json.getInt("queued");

    if (strcmp(point, "gamepad_state") == 0 || strcmp(point, "all") == 0) {
        cleared += inj.gamepadRawInputQueueCount();
        inj.clearGamepadRawInputQueue();
    }
    if (strcmp(point, "hid_report") == 0 || strcmp(point, "all") == 0) {
        cleared += inj.hidInjectQueueCount();
        inj.clearHIDInjectQueue();
    }

    Json resp;
    resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
    resp.printf("{status:%Q,cmd:%Q,queued:%d,cleared:%d}",
                "ok", cmd, queued + 1, cleared);
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

static void handleSetMode(const char *cmd, const Json &json) {
    uint8_t modeVal = static_cast<uint8_t>(json.getInt("mode", 0));
    auto &inj = TestInjector::getInstance();
    int queued = json.getInt("queued");

    if (modeVal > 2) {
        Json resp;
        resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
        resp.printf("{status:%Q,cmd:%Q,queued:%d,error_code:%d,reason:%Q}",
                    "error", cmd, queued + 1,
                    static_cast<int>(Proto::ErrorCode::ERR_INTERNAL),
                    "Invalid mode value");
        uint16_t len = resp.end();
        FrameLayer::getInstance().sendResponse(resp.c_str(), len);
        return;
    }
    inj.setMode(static_cast<TestInjector::TestMode>(modeVal));

    Json resp;
    resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
    resp.printf("{status:%Q,cmd:%Q,queued:%d,mode:%d,mode_name:%Q}",
                "ok", cmd, queued + 1, modeVal,
                TestInjector::modeName(inj.getMode()));
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

static void handleGetMode(const char *cmd, const Json &json) {
    auto &inj = TestInjector::getInstance();
    int queued = json.getInt("queued");

    Json resp;
    resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
    resp.printf("{status:%Q,cmd:%Q,queued:%d,mode:%d,mode_name:%Q}",
                "ok", cmd, queued + 1,
                static_cast<int>(inj.getMode()),
                TestInjector::modeName(inj.getMode()));
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

static void handleGetHistory(const char *cmd, const Json &json) {
    char typeBuf[32];
    const char *historyType = json.getStrCopy("type", typeBuf, sizeof(typeBuf));
    if (!historyType) historyType = "gamepad_state";
    uint8_t count = static_cast<uint8_t>(json.getInt("count", 10));
    if (count > 64) count = 64;
    auto &inj = TestInjector::getInstance();
    int queued = json.getInt("queued");

    if (strcmp(historyType, "gamepad_state") == 0) {
        TestInjector::GamepadRawInputEntry rawEntries[64];
        uint8_t actual = inj.readGamepadHistory(rawEntries, count);

        // Build JSON output
        Json resp;
        resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
        resp.printf("{status:%Q,cmd:%Q,queued:%d,type:%Q,count:%d,entries:[",
                    "ok", cmd, queued + 1, historyType, actual);
        for (uint8_t i = 0; i < actual; ++i) {
            if (i > 0) resp.printf(",");
            resp.printf("{buttons:%lu,dpad:%d,"
                        "lx:%d,ly:%d,rx:%d,ry:%d,lt:%d,rt:%d}",
                        (unsigned long)rawEntries[i].state.buttons,
                        rawEntries[i].state.dpad,
                        rawEntries[i].state.lx, rawEntries[i].state.ly,
                        rawEntries[i].state.rx, rawEntries[i].state.ry,
                        rawEntries[i].state.lt, rawEntries[i].state.rt);
        }
        resp.printf("]}");
        uint16_t len = resp.end();
        FrameLayer::getInstance().sendResponse(resp.c_str(), len);

    } else if (strcmp(historyType, "hid_report") == 0) {
        TestInjector::HIDReportEntry rawEntries[64];
        uint8_t actual = inj.readHIDHistory(rawEntries, count);

        Json resp;
        resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
        resp.printf("{status:%Q,cmd:%Q,queued:%d,type:%Q,count:%d,entries:[",
                    "ok", cmd, queued + 1, historyType, actual);
        for (uint8_t i = 0; i < actual; ++i) {
            if (i > 0) resp.printf(",");
            resp.printf("{buttons:%lu,dpad:%d,"
                        "l_x_axis:%d,l_y_axis:%d,r_x_axis:%d,r_y_axis:%d}",
                        (unsigned long)rawEntries[i].report.buttons,
                        rawEntries[i].report.direction,
                        rawEntries[i].report.l_x_axis,
                        rawEntries[i].report.l_y_axis,
                        rawEntries[i].report.r_x_axis,
                        rawEntries[i].report.r_y_axis);
        }
        resp.printf("]}");
        uint16_t len = resp.end();
        FrameLayer::getInstance().sendResponse(resp.c_str(), len);

    } else {
        Json resp;
        resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
        resp.printf("{status:%Q,cmd:%Q,queued:%d,type:%Q,error_code:%d,"
                    "reason:%Q}",
                    "error", cmd, queued + 1, historyType,
                    static_cast<int>(Proto::ErrorCode::ERR_INTERNAL),
                    "Invalid history type");
        uint16_t len = resp.end();
        FrameLayer::getInstance().sendResponse(resp.c_str(), len);
    }
}

static void handleClearHistory(const char *cmd, const Json &json) {
    char typeBuf[32];
    const char *historyType = json.getStrCopy("type", typeBuf, sizeof(typeBuf));
    if (!historyType) historyType = "gamepad_state";
    auto &inj = TestInjector::getInstance();
    int queued = json.getInt("queued");

    if (strcmp(historyType, "gamepad_state") == 0 || strcmp(historyType, "all") == 0) {
        inj.clearGamepadHistory();
    }
    if (strcmp(historyType, "hid_report") == 0 || strcmp(historyType, "all") == 0) {
        inj.clearHIDHistory();
    }

    Json resp;
    resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
    resp.printf("{status:%Q,cmd:%Q,queued:%d,type:%Q}",
                "ok", cmd, queued + 1, historyType);
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

static void handleGetStatus(const char *cmd, const Json &json) {
    auto &inj = TestInjector::getInstance();
    uint8_t mode = static_cast<uint8_t>(inj.getMode());
    const char *modeName = TestInjector::modeName(inj.getMode());
    bool overrideGp = inj.isOverrideGamepadRawInput();
    bool overrideHid = inj.isOverrideHIDReport();
    uint8_t gpHistCount = inj.getGamepadHistoryCount();
    uint8_t hidHistCount = inj.getHIDHistoryCount();
    uint8_t gpQueued = inj.gamepadRawInputQueueCount();
    uint8_t hidQueued = inj.hidInjectQueueCount();
    int queued = json.getInt("queued");

    Json resp;
    resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
    resp.printf("{status:%Q,cmd:%Q,queued:%d,mode:%d,mode_name:%Q,"
                "override_gamepad_state:%B,override_hid_report:%B,"
                "gamepad_history_count:%d,hid_history_count:%d,"
                "gamepad_inject_queued:%d,hid_inject_queued:%d}",
                "ok", cmd, queued + 1, mode, modeName,
                overrideGp, overrideHid,
                gpHistCount, hidHistCount,
                gpQueued, hidQueued);
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

static void handleReset(const char *cmd, const Json &json) {
    auto &inj = TestInjector::getInstance();
    inj.reset();
    int queued = json.getInt("queued");

    Json resp;
    resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
    resp.printf("{status:%Q,cmd:%Q,queued:%d}",
                "ok", cmd, queued + 1);
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

// ── Flash info (for testing only) ──

static void handleFlashInfo(const char *cmd, const Json &json) {
    auto &flash = Drivers::Device::FlashW25qxx::getInstance();
    const auto &info = flash.getInfo();
    int queued = json.getInt("queued");

    Json resp;
    resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
    resp.printf("{status:%Q,cmd:%Q,queued:%d,"
                "sizeBytes:%lu,pageSize:%u,sectorSize:%lu,"
                "init:%d}",
                "ok", cmd, queued + 1,
                (unsigned long)info.sizeBytes, info.pageSize,
                (unsigned long)info.sectorSize,
                flash.isInitialized());
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

// ── Flash chip erase (for testing only) ──

static void handleChipErase(const char *cmd, const Json &json) {
    auto &flash = Drivers::Device::FlashW25qxx::getInstance();
    bool ok = flash.eraseChip();
    int queued = json.getInt("queued");

    Json resp;
    resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
    if (ok) {
        resp.printf("{status:%Q,cmd:%Q,queued:%d,warning:%Q}",
                    "ok", cmd, queued + 1,
                    "chip_erase is dangerous — entire SPI flash wiped");
    } else {
        resp.printf("{status:%Q,cmd:%Q,queued:%d,error_code:%d,reason:%Q}",
                    "error", cmd, queued + 1, 1, "eraseChip failed");
    }
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

static void handleSpiMode(const char *cmd, const Json &json) {
    int modeVal = json.getInt("mode");
    int queued = json.getInt("queued");

    auto mode = static_cast<Drivers::Peripheral::BUS::Mode>(modeVal);
    auto &flash = Drivers::Device::FlashW25qxx::getInstance();
    flash.setSpiBusMode(mode);

    Json resp;
    resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
    resp.printf("{status:%Q,cmd:%Q,queued:%d,mode:%d}",
                "ok", cmd, queued + 1, modeVal);
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

static void handleFlashRead(const char *cmd, const Json &json) {
    int addr = json.getInt("addr");
    int len = json.getInt("len");
    int queued = json.getInt("queued");

    auto &flash = Drivers::Device::FlashW25qxx::getInstance();
    bool ok = false;

    if (len > 0 && len <= 4096 && addr >= 0) {
        uint8_t buf[4096];
        ok = flash.read(static_cast<uint32_t>(addr), buf,
                         static_cast<uint32_t>(len));
        Json resp;
        resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
        if (ok) {
            resp.printf("{status:%Q,cmd:%Q,queued:%d,lenRead:%d}",
                        "ok", cmd, queued + 1, len);
        } else {
            resp.printf("{status:%Q,cmd:%Q,queued:%d,error_code:%d,"
                        "reason:%Q}",
                        "error", cmd, queued + 1, 1, "read failed");
        }
        uint16_t slen = resp.end();
        FrameLayer::getInstance().sendResponse(resp.c_str(), slen);
    } else {
        Json resp;
        resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
        resp.printf("{status:%Q,cmd:%Q,queued:%d,error_code:%d,reason:%Q}",
                    "error", cmd, queued + 1, 1, "invalid addr/len");
        uint16_t slen = resp.end();
        FrameLayer::getInstance().sendResponse(resp.c_str(), slen);
    }
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
void TestCmdHandler::handle(const char *cmd, const Json &json) {
    int queued = json.getInt("queued");
    LOG_DEBUG("TestCmdHandler: cmd='%s' queued=%d", cmd, queued);

    // Non-protocol commands handled directly
    if (strcmp(cmd, "test.chip_erase") == 0) {
        handleChipErase(cmd, json);
        return;
    }
    if (strcmp(cmd, "test.spi_mode") == 0) {
        handleSpiMode(cmd, json);
        return;
    }
    if (strcmp(cmd, "test.flash_read") == 0) {
        handleFlashRead(cmd, json);
        return;
    }
    if (strcmp(cmd, "test.flash_info") == 0) {
        handleFlashInfo(cmd, json);
        return;
    }
    if (strcmp(cmd, "test.mempool_info") == 0) {
        int queued = json.getInt("queued");
        using namespace ThetaGP::Mempool;
        auto &dm = Drivers::Device::DevMem::getInstance();
        auto pid = dm.poolId();
        auto stats = (pid != INVALID_POOL_ID) ? MempoolManager::poolStats(pid) : PoolStats{0,0,0,0,0};
        Json resp;
        resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
        resp.printf("{status:%Q,cmd:%Q,queued:%d,"
                    "poolId:%d,total:%u,used:%u,free:%u}",
                    "ok", cmd, queued + 1,
                    static_cast<int>(pid),
                    stats.totalSize, stats.usedSize, stats.freeSize);
        uint16_t len = resp.end();
        FrameLayer::getInstance().sendResponse(resp.c_str(), len);
        return;
    }

    if (!Proto::dispatch(cmd, json)) {
        // Unknown test command — return error response
        int queued = json.getInt("queued");
        Json resp;
        resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
        resp.printf("{status:%Q,cmd:%Q,queued:%d,error_code:%d,reason:%Q}",
                    "error", cmd, queued + 1,
                    static_cast<int>(Proto::ErrorCode::ERR_UNKNOWN_CMD),
                    "unknown command");
        uint16_t len = resp.end();
        FrameLayer::getInstance().sendResponse(resp.c_str(), len);
    }
}

#else

// All methods are inlined in testcmds.h for production mode
// (empty class stub with no-op implementations)

#endif // THETAGP_CFG_TEST

} // namespace ThetaGP::Test
