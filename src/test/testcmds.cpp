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
#include "test/framelayer.h"

#include "drivers/device/flash/flash_w25qxx.h"
#include "drivers/device/devmem.h"

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
