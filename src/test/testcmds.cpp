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
#include "conf/ThetaGP_Config.h" // THETAGP_CFG_HAS_FLASH and THETAGP_CFG_BUILD_TEST_API, the firmware-layer switches this file reads
#include "test/dispatcher.h"
#include "test/framelayer.h"

#include "drivers/device/flash/flash_w25qxx.h"
#include "drivers/device/keypad.h"
#include "drivers/device/system_timer.h"
#include "gamepad/config/config_manager.h"
#include "gamepad/profile/profile_store.h"

#include "utils/log/log.h"
#include "utils/mem_info.h"

#include "protocol/proto.h"

#include <cstring>

namespace ThetaGP::Test {

#if THETAGP_CFG_BUILD_TEST_API

static COMMON_ZERO_INIT char s_testRespBuf[4096];

TestCmdHandler &TestCmdHandler::getInstance() {
    static TestCmdHandler instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Handler functions (CommandHandler signature)
// ---------------------------------------------------------------------------

// ── Flash info (for testing only) ──

#if THETAGP_CFG_HAS_FLASH

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

#endif // THETAGP_CFG_HAS_FLASH

// ── test.mem_info ──
// Raw linker region report: base/end/size/used for the six regions, plus the
// reserves and the live RAM total.

static void handleMemInfo(const char *cmd, const Json &json) {
    using namespace ThetaGP::Util::MemInfo;

    const auto f  = region(RegionId::Flash);
    const auto dt = region(RegionId::Dtcm);
    const auto ax = region(RegionId::Axi);
    const auto d2 = region(RegionId::D2);
    const auto d3 = region(RegionId::D3);
    const auto it = region(RegionId::Itcm);
    int queued = json.getInt("queued");

    Json resp;
    resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
    resp.printf("{status:%Q,cmd:%Q,queued:%d,"
                "flash_base:%lu,flash_end:%lu,flash_size:%lu,flash_used:%lu,"
                "dtcm_base:%lu,dtcm_end:%lu,dtcm_size:%lu,dtcm_used:%lu,"
                "axi_base:%lu,axi_end:%lu,axi_size:%lu,axi_used:%lu,"
                "d2_base:%lu,d2_end:%lu,d2_size:%lu,d2_used:%lu,"
                "d3_base:%lu,d3_end:%lu,d3_size:%lu,d3_used:%lu,"
                "itcm_base:%lu,itcm_end:%lu,itcm_size:%lu,itcm_used:%lu,"
                "ram_reserved_bytes:%lu,stack_bytes:%lu,heap_bytes:%lu,"
                "ram_live_bytes:%lu}",
                "ok", cmd, queued + 1,
                (unsigned long)f.base, (unsigned long)f.end,
                (unsigned long)f.size, (unsigned long)f.used,
                (unsigned long)dt.base, (unsigned long)dt.end,
                (unsigned long)dt.size, (unsigned long)dt.used,
                (unsigned long)ax.base, (unsigned long)ax.end,
                (unsigned long)ax.size, (unsigned long)ax.used,
                (unsigned long)d2.base, (unsigned long)d2.end,
                (unsigned long)d2.size, (unsigned long)d2.used,
                (unsigned long)d3.base, (unsigned long)d3.end,
                (unsigned long)d3.size, (unsigned long)d3.used,
                (unsigned long)it.base, (unsigned long)it.end,
                (unsigned long)it.size, (unsigned long)it.used,
                (unsigned long)ramReservedBytes(),
                (unsigned long)stackBytes(),
                (unsigned long)heapBytes(),
                (unsigned long)ramLiveBytes());
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

// ── Flash chip erase (for testing only) ──

#if THETAGP_CFG_HAS_FLASH

static void handleChipErase(const char *cmd, const Json &json) {
    auto &flash = Drivers::Device::FlashW25qxx::getInstance();
    bool ok = flash.eraseChip();
    int queued = json.getInt("queued");

    Json resp;
    resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
    if (ok) {
        // Flash is now fully erased. Re-scan the profile system so the runtime
        // caches (nextAddr, addresses, count) describe the empty flash instead
        // of reporting pre-erase data — and then let the configuration layer do
        // what it does on a fresh flash: write the factory Profile0 back and
        // reset the active configuration to it. Without the second call the
        // chip would sit with no Profile0 until the next reboot, and RAM would
        // keep describing profiles the erase removed. The same call runs from
        // ConfigManager::init(), so both paths leave the chip in one state.
        ThetaGP::Gamepad::Profile::ProfileStore::getInstance().init();
        (void)ThetaGP::Gamepad::Config::ConfigManager::getInstance()
            .ensureFactoryProfile();
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

static void handleEraseSector(const char *cmd, const Json &json) {
    int addr = json.getInt("addr");
    int queued = json.getInt("queued");
    auto &flash = Drivers::Device::FlashW25qxx::getInstance();
    bool ok = flash.eraseSector(static_cast<uint32_t>(addr));
    Json resp;
    resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
    resp.printf("{status:%Q,cmd:%Q,queued:%d,addr:0x%X,ok:%d}",
                "ok", cmd, queued + 1, addr, ok ? 1 : 0);
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

static void handleCompaction(const char *cmd, const Json &json) {
    int queued = json.getInt("queued");
    bool ok = ThetaGP::Gamepad::Profile::ProfileStore::getInstance().compaction();
    Json resp;
    resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
    resp.printf("{status:%Q,cmd:%Q,queued:%d,ok:%d}",
                "ok", cmd, queued + 1, ok ? 1 : 0);
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

#endif // THETAGP_CFG_HAS_FLASH

// ── test.keypad_scan ──
// Keypad scan time as the device layer's clock (SystemTimer::getCycleCounter) saw
// it inside Keypad::scanCallback. The counters are cycles — that counter ticks
// once per CPU cycle, so a ten-microsecond callback is measured to the cycle
// rather than to the microsecond — and the response carries the clock's own
// cycles-per-microsecond factor, so a host converts any count without being told
// the CPU frequency. Converting last/max happens here, once per command, instead
// of in the scan path. A test-domain command: it is hand-dispatched below rather
// than declared in protocol.toml, like the other test.* commands.
//
// The same response carries the commit counters of ADR-0006 O-6:
// `commit_count` is every committed state change since boot (a press commit and
// a release commit per tap, so a tap that never commits shows up as a count that
// did not move), and `max_commit_latency_us` is the longest one of them took,
// converted here from the driver's unit of scans through the config's scan rate.

static uint32_t commitLatencyUs(const Drivers::Device::Keypad::CommitStats &stats) {
    // The driver holds lateness in scans and does not divide; the conversion
    // belongs to the readout, where the scan rate is a constant and this runs
    // once per command. Scans first, then the division: the other order would
    // floor the 32 000 Hz scan period to 31 µs and lose a microsecond per scan.
    return (uint32_t)stats.max_latency_scans * 1000000UL /
           Drivers::Device::KeypadConfig::DEFAULT_SCAN_FREQ;
}

static void handleKeypadScan(const char *cmd, const Json &json) {
    auto &timer = Drivers::Device::SystemTimer::getInstance();

    Drivers::Device::Keypad::ScanStats stats;
    Drivers::Device::Keypad::getInstance().getScanStats(stats);

    Drivers::Device::Keypad::CommitStats commits;
    Drivers::Device::Keypad::getInstance().getCommitStats(commits);

    int queued = json.getInt("queued");

    Json resp;
    resp.beginWrite(s_testRespBuf, sizeof(s_testRespBuf));
    resp.printf("{status:%Q,cmd:%Q,queued:%d,"
                "scan_hz:%lu,drive_lines:%lu,sense_lines:%lu,cycles_per_us:%lu,"
                "count:%lu,last_cycles:%lu,max_cycles:%lu,"
                "last_us:%lu,max_us:%lu,"
                "commit_count:%lu,max_commit_latency_us:%lu,"
                "sum_cycles:",
                "ok", cmd, queued + 1,
                (unsigned long)Drivers::Device::KeypadConfig::DEFAULT_SCAN_FREQ,
                (unsigned long)Drivers::Device::Keypad::getDriveLineCount(),
                (unsigned long)Drivers::Device::Keypad::getSenseLineCount(),
                // The clock's own conversion factor, so a host converts any of
                // the counts below without being told the CPU frequency.
                (unsigned long)timer.microsToCycles(1),
                (unsigned long)stats.count,
                (unsigned long)stats.last_cycles,
                (unsigned long)stats.max_cycles,
                (unsigned long)timer.cyclesToMicros((int32_t)stats.last_cycles),
                (unsigned long)timer.cyclesToMicros((int32_t)stats.max_cycles),
                (unsigned long)commits.count,
                (unsigned long)commitLatencyUs(commits));
    // The sum is cycles and 64-bit; this toolchain's printf carries no long long
    // conversion — a "%llu" reaches the output as the literal "lu" — so the value
    // is written as decimal digits in three 32-bit chunks rather than being
    // widened inside a format string.
    const uint32_t low9 = (uint32_t)(stats.sum_cycles % 1000000000ULL);
    const uint64_t rest = stats.sum_cycles / 1000000000ULL;
    const uint32_t mid9 = (uint32_t)(rest % 1000000000ULL);
    const uint32_t high = (uint32_t)(rest / 1000000000ULL);
    if (high != 0) {
        resp.printf("%lu%09lu%09lu}", (unsigned long)high,
                    (unsigned long)mid9, (unsigned long)low9);
    } else if (mid9 != 0) {
        resp.printf("%lu%09lu}", (unsigned long)mid9,
                    (unsigned long)low9);
    } else {
        resp.printf("%lu}", (unsigned long)low9);
    }
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
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
#if THETAGP_CFG_HAS_FLASH
    if (strcmp(cmd, "test.chip_erase") == 0) {
        handleChipErase(cmd, json);
        return;
    }
    if (strcmp(cmd, "test.erase_sector") == 0) {
        handleEraseSector(cmd, json);
        return;
    }
    if (strcmp(cmd, "test.compaction") == 0) {
        handleCompaction(cmd, json);
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
#endif // THETAGP_CFG_HAS_FLASH
    if (strcmp(cmd, "test.mem_info") == 0) {
        handleMemInfo(cmd, json);
        return;
    }
    if (strcmp(cmd, "test.keypad_scan") == 0) {
        handleKeypadScan(cmd, json);
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

#endif // THETAGP_CFG_BUILD_TEST_API

} // namespace ThetaGP::Test
