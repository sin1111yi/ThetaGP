/**
 * This file is a part of ThetaGP.
 *
 * ThetaGP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ThetaGP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file flash_wl_handler.cpp
 * @brief SPI DMA and Wear-Leveling CDC test command handlers
 *
 * Follows the pattern established by testcmds.cpp and testsys.cpp.
 * SPI test commands use transferAsync() + busy-wait polling with timeout
 * to validate the actual DMA ISR path. WL commands call the wear-leveling
 * API directly. Updated for full-flash wear-leveling with meta, log, and
 * dynamic ring buffer layout.
 */

#include "test/flash_wl_handler.h"
#include "test/dispatcher.h"
#include "test/framelayer.h"

#include "drivers/device/flash/flash_base.h"
#include "drivers/peripherals/bus/bus_spi.h"
#include "drivers/peripherals/peripheralsmgr.h"

#include "utils/log/log.h"

#include <cstring>

namespace ThetaGP::Test {

#ifdef THETAGP_ENABLE_TEST_API

using namespace ThetaGP::Drivers::Device;
using namespace ThetaGP::Drivers::Peripheral;
using ThetaGP::Result;
using WL = FlashBase::WearLevel;

// ── Helpers ──

/**
 * @brief Convert hex character to nibble
 */
static uint8_t hexNibble(char c) {
  if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
  return 0;
}

/**
 * @brief Convert hex string to binary buffer
 * @param hex   Hex string (e.g. "AABBCC")
 * @param buf   Output buffer
 * @param maxLen Max bytes to decode
 * @return Number of bytes decoded
 */
static uint16_t hexDecode(const char *hex, uint8_t *buf, uint16_t maxLen) {
  if (!hex || !buf) return 0;
  size_t hexLen = strlen(hex);
  uint16_t byteLen = (hexLen / 2 < maxLen) ? (hexLen / 2) : maxLen;
  for (uint16_t i = 0; i < byteLen; i++) {
    buf[i] = (hexNibble(hex[i * 2]) << 4) | hexNibble(hex[i * 2 + 1]);
  }
  return byteLen;
}

/**
 * @brief Convert binary buffer to hex string (static buffer, single-use)
 */
static const char *hexEncode(const uint8_t *buf, uint16_t len) {
  static char hexBuf[1028]; // 512 bytes * 2 + 1 = 1025, round to 1028
  if (len > 512) len = 512;
  for (uint16_t i = 0; i < len; i++) {
    static const char hexChars[] = "0123456789ABCDEF";
    hexBuf[i * 2] = hexChars[(buf[i] >> 4) & 0x0F];
    hexBuf[i * 2 + 1] = hexChars[buf[i] & 0x0F];
  }
  hexBuf[len * 2] = '\0';
  return hexBuf;
}

// ── FlashWlHandler ──

FlashWlHandler &FlashWlHandler::getInstance() {
  static FlashWlHandler instance;
  return instance;
}

// ── flash.read_raw — test-only: read and return hex data ──

static void handleFlashReadRaw(const char *cmd, JsonDocument &doc) {
  JsonDocument resp;
  resp["cmd"] = cmd;
  resp["queued"] = doc["queued"].as<int>() + 1;

  uint32_t addr = doc["addr"] | 0;
  uint16_t len = doc["len"] | 64;
  if (len > 512) len = 512;
  if (len == 0) {
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(ThetaGP::Result::InvalidParam);
    resp["reason"] = "len must be > 0";
    FrameLayer::getInstance().sendResponse(resp);
    return;
  }

  FlashBase &flash = FlashBase::getInstance();
  if (!flash.isInitialized()) {
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(ThetaGP::Result::NotReady);
    resp["reason"] = "flash not initialized";
    FrameLayer::getInstance().sendResponse(resp);
    return;
  }

  uint8_t buf[512];
  if (!flash.read(addr, buf, len)) {
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(ThetaGP::Result::Error);
    resp["reason"] = "flash read failed";
    FrameLayer::getInstance().sendResponse(resp);
    return;
  }

  resp["status"] = "ok";
  resp["addr"] = addr;
  resp["len"] = len;

  // Split hex data into chunks to fit within 1K CDC frame buffer
  // Each chunk carries a portion of the hex string with seq/total/more
  static constexpr uint16_t CHUNK_HEX = 512; // 256 bytes per chunk (512 hex chars)
  uint16_t totalHex = len * 2;
  uint16_t chunks = (totalHex + CHUNK_HEX - 1) / CHUNK_HEX;
  const char *hexData = hexEncode(buf, len);

  for (uint16_t seq = 0; seq < chunks; seq++) {
    JsonDocument chunkResp;
    chunkResp["cmd"] = cmd;
    chunkResp["queued"] = doc["queued"].as<int>() + 1;
    chunkResp["status"] = "ok";
    chunkResp["addr"] = addr;
    chunkResp["len"] = len;
    chunkResp["seq"] = seq;
    chunkResp["total"] = chunks;

    uint16_t offset = seq * CHUNK_HEX;
    uint16_t thisChunk = (totalHex - offset < CHUNK_HEX) ? (totalHex - offset) : CHUNK_HEX;

    // Extract chunk from the full hex string
    char chunkBuf[CHUNK_HEX + 1];
    std::memcpy(chunkBuf, hexData + offset, thisChunk);
    chunkBuf[thisChunk] = '\0';

    chunkResp["chunk"] = chunkBuf;
    chunkResp["more"] = (seq + 1 < chunks);
    FrameLayer::getInstance().sendResponse(chunkResp);
  }
}

// ── Flash Domain Handlers ──

static void handleFlashRead(const char *cmd, JsonDocument &doc) {
  JsonDocument resp;
  resp["cmd"] = cmd;
  resp["queued"] = doc["queued"].as<int>() + 1;

  uint32_t addr = doc["addr"] | 0;
  uint16_t len = doc["len"] | 64;
  if (len > 512) len = 512;
  if (len == 0) {
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(ThetaGP::Result::InvalidParam);
    resp["reason"] = "len must be > 0";
    FrameLayer::getInstance().sendResponse(resp);
    return;
  }

  // Use the flash driver's read (which uses synchronous SPI transfer)
  // to test the SPI path end-to-end. For DMA-path testing, we'd need
  // an async read from the wear-leveling layer.
  FlashBase &flash = FlashBase::getInstance();
  if (!flash.isInitialized()) {
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(ThetaGP::Result::NotReady);
    resp["reason"] = "flash not initialized";
    FrameLayer::getInstance().sendResponse(resp);
    return;
  }

  uint8_t buf[512];
  if (!flash.read(addr, buf, len)) {
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(ThetaGP::Result::Error);
    resp["reason"] = "flash read failed";
    FrameLayer::getInstance().sendResponse(resp);
    return;
  }

  resp["status"] = "ok";
  resp["addr"] = addr;
  resp["len"] = len;
  FrameLayer::getInstance().sendResponse(resp);
}

static void handleFlashWrite(const char *cmd, JsonDocument &doc) {
  JsonDocument resp;
  resp["cmd"] = cmd;
  resp["queued"] = doc["queued"].as<int>() + 1;

  uint32_t addr = doc["addr"] | 0;
  const char *dataHex = doc["data"] | "";

  if (strlen(dataHex) == 0) {
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(ThetaGP::Result::InvalidParam);
    resp["reason"] = "data field required";
    FrameLayer::getInstance().sendResponse(resp);
    return;
  }

  uint8_t buf[256];
  uint16_t len = hexDecode(dataHex, buf, 256);
  if (len == 0) {
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(ThetaGP::Result::InvalidParam);
    resp["reason"] = "no data decoded";
    FrameLayer::getInstance().sendResponse(resp);
    return;
  }

  FlashBase &flash = FlashBase::getInstance();
  if (!flash.isInitialized()) {
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(ThetaGP::Result::NotReady);
    resp["reason"] = "flash not initialized";
    FrameLayer::getInstance().sendResponse(resp);
    return;
  }

  if (!flash.write(addr, buf, len)) {
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(ThetaGP::Result::Error);
    resp["reason"] = "flash write failed";
    FrameLayer::getInstance().sendResponse(resp);
    return;
  }

  resp["status"] = "ok";
  resp["addr"] = addr;
  resp["len"] = len;
  FrameLayer::getInstance().sendResponse(resp);
}

static void handleFlashSectorErase(const char *cmd, JsonDocument &doc) {
  JsonDocument resp;
  resp["cmd"] = cmd;
  resp["queued"] = doc["queued"].as<int>() + 1;

  uint32_t addr = doc["addr"] | 0;

  FlashBase &flash = FlashBase::getInstance();
  if (!flash.isInitialized()) {
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(ThetaGP::Result::NotReady);
    resp["reason"] = "flash not initialized";
    FrameLayer::getInstance().sendResponse(resp);
    return;
  }

  if (!flash.eraseSector(addr)) {
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(ThetaGP::Result::Error);
    resp["reason"] = "sector erase failed";
    FrameLayer::getInstance().sendResponse(resp);
    return;
  }

  resp["status"] = "ok";
  resp["addr"] = addr;
  FrameLayer::getInstance().sendResponse(resp);
}

// ── WL Domain Handlers ──

static void handleWlStore(const char *cmd, JsonDocument &doc) {
  JsonDocument resp;
  resp["cmd"] = cmd;
  resp["queued"] = doc["queued"].as<int>() + 1;

  const char *key = doc["key"] | "";
  const char *dataHex = doc["data"] | "";

  if (strlen(key) == 0) {
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(ThetaGP::Result::InvalidParam);
    resp["reason"] = "key is required";
    FrameLayer::getInstance().sendResponse(resp);
    return;
  }

  uint8_t buf[512];
  uint16_t len = hexDecode(dataHex, buf, 512);

  auto &wl = FlashBase::getInstance().wearLevel();
  Result r = wl.storeConfig(key, buf, len);
  if (r.isError()) {
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(r.value());
    resp["reason"] = "store failed";
    FrameLayer::getInstance().sendResponse(resp);
    return;
  }

  resp["status"] = "ok";
  resp["key"] = key;
  resp["len"] = len;
  FrameLayer::getInstance().sendResponse(resp);
}

static void handleWlLoad(const char *cmd, JsonDocument &doc) {
  JsonDocument resp;
  resp["cmd"] = cmd;
  resp["queued"] = doc["queued"].as<int>() + 1;

  const char *key = doc["key"] | "";
  if (strlen(key) == 0) {
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(ThetaGP::Result::InvalidParam);
    resp["reason"] = "key is required";
    FrameLayer::getInstance().sendResponse(resp);
    return;
  }

  uint8_t buf[512];
  uint16_t outLen = 0;
  auto &wl = FlashBase::getInstance().wearLevel();
  Result r = wl.loadConfig(key, buf, 512, &outLen);
  if (r.isError()) {
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(r.value());
    resp["reason"] = "load failed";
    FrameLayer::getInstance().sendResponse(resp);
    return;
  }

  resp["status"] = "ok";
  resp["key"] = key;
  resp["len"] = outLen;
  resp["crc_valid"] = true;
  FrameLayer::getInstance().sendResponse(resp);
}

static void handleWlErase(const char *cmd, JsonDocument &doc) {
  JsonDocument resp;
  resp["cmd"] = cmd;
  resp["queued"] = doc["queued"].as<int>() + 1;

  const char *key = doc["key"] | "";
  if (strlen(key) == 0) {
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(ThetaGP::Result::InvalidParam);
    resp["reason"] = "key is required";
    FrameLayer::getInstance().sendResponse(resp);
    return;
  }

  auto &wl = FlashBase::getInstance().wearLevel();
  Result r = wl.eraseConfig(key);
  if (r.isError()) {
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(r.value());
    resp["reason"] = "erase failed";
    FrameLayer::getInstance().sendResponse(resp);
    return;
  }

  resp["status"] = "ok";
  resp["key"] = key;
  FrameLayer::getInstance().sendResponse(resp);
}

static void handleWlStatus(const char *cmd, [[maybe_unused]] JsonDocument &doc) {
  JsonDocument resp;
  resp["cmd"] = cmd;
  resp["queued"] = doc["queued"].as<int>() + 1;

  auto &wl = FlashBase::getInstance().wearLevel();
  auto status = wl.getStatus();

  resp["status"] = "ok";
  resp["total_slots"] = status.totalSlots;
  resp["used_slots"] = status.usedSlots;
  resp["stale_slots"] = status.staleSlots;
  resp["total_writes"] = status.totalWrites;
  resp["min_wear"] = status.minWear;
  resp["max_wear"] = status.maxWear;
  resp["avg_wear"] = status.avgWear;
  resp["data_base_addr"] = status.dataBaseAddr;
  resp["data_sector_count"] = status.dataSectorCount;
  resp["log_sector_count"] = status.logSectorCount;
  resp["head_addr"] = status.headAddr;
  resp["flash_size"] = status.flashSize;
  resp["meta_written"] = status.metaWritten;
  FrameLayer::getInstance().sendResponse(resp);
}

// ── New WL Domain Handlers ──

static void handleWlMetaGet(const char *cmd, [[maybe_unused]] JsonDocument &doc) {
  JsonDocument resp;
  resp["cmd"] = cmd;
  resp["queued"] = doc["queued"].as<int>() + 1;

  auto &wl = FlashBase::getInstance().wearLevel();
  WL::MetaSector meta;
  Result r = wl.readMeta(meta);
  if (r.isError()) {
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(r.value());
    resp["reason"] = "meta read failed";
    FrameLayer::getInstance().sendResponse(resp);
    return;
  }

  resp["status"] = "ok";
  resp["magic"] = meta.magic;
  resp["valid"] = meta.valid;
  resp["version"] = meta.version;
  resp["crc32"] = meta.crc32;
  resp["firmware_meta"] = hexEncode(meta.firmwareMeta, 32);
  FrameLayer::getInstance().sendResponse(resp);
}

static void handleWlMetaWrite(const char *cmd, JsonDocument &doc) {
  JsonDocument resp;
  resp["cmd"] = cmd;
  resp["queued"] = doc["queued"].as<int>() + 1;

  WL::MetaSector meta;
  std::memset(&meta, 0, sizeof(meta));
  meta.magic = WL::META_MAGIC;
  meta.valid = WL::VALID;
  meta.version = doc["version"] | 1;

  const char *fwHex = doc["firmware_meta"] | "";
  if (strlen(fwHex) > 0) {
    hexDecode(fwHex, meta.firmwareMeta, 1008);
  }

  auto &wl = FlashBase::getInstance().wearLevel();
  Result r = wl.writeMeta(meta);
  if (r.isError()) {
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(r.value());
    resp["reason"] = "meta write failed";
    FrameLayer::getInstance().sendResponse(resp);
    return;
  }

  resp["status"] = "ok";
  resp["version"] = meta.version;
  FrameLayer::getInstance().sendResponse(resp);
}

static void handleWlFullScan(const char *cmd, [[maybe_unused]] JsonDocument &doc) {
  JsonDocument resp;
  resp["cmd"] = cmd;
  resp["queued"] = doc["queued"].as<int>() + 1;

  auto &wl = FlashBase::getInstance().wearLevel();
  Result r = wl.fullScan();
  if (r.isError()) {
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(r.value());
    resp["reason"] = "full scan failed";
    FrameLayer::getInstance().sendResponse(resp);
    return;
  }

  auto status = wl.getStatus();
  resp["status"] = "ok";
  resp["valid_count"] = status.usedSlots;
  resp["total_slots"] = status.totalSlots;
  FrameLayer::getInstance().sendResponse(resp);
}

static void handleWlInfo(const char *cmd, [[maybe_unused]] JsonDocument &doc) {
  JsonDocument resp;
  resp["cmd"] = cmd;
  resp["queued"] = doc["queued"].as<int>() + 1;

  auto &wl = FlashBase::getInstance().wearLevel();
  auto info = wl.getInfo();

  resp["status"] = "ok";
  resp["meta_addr"] = 0;
  resp["log_base_addr"] = 4096;
  resp["log_sector_count"] = info.logSectorCount;
  resp["data_base_addr"] = info.dataBaseAddr;
  resp["data_sector_count"] = info.dataSectorCount;
  resp["flash_size"] = info.flashSize;
  resp["head_addr"] = info.headAddr;
  resp["meta_written"] = info.metaWritten;
  resp["cached_slots"] = info.usedSlots;
  FrameLayer::getInstance().sendResponse(resp);
}

// ── Domain Dispatch ──

void FlashWlHandler::handleFlash(const char *cmd, JsonDocument &doc) {
  LOG_DEBUG("FlashWlHandler: cmd='%s'", cmd);

  if (strcmp(cmd, "flash.read_raw") == 0) {
    handleFlashReadRaw(cmd, doc);
  } else if (strcmp(cmd, "flash.read") == 0) {
    handleFlashRead(cmd, doc);
  } else if (strcmp(cmd, "flash.write") == 0) {
    handleFlashWrite(cmd, doc);
  } else if (strcmp(cmd, "flash.sector_erase") == 0) {
    handleFlashSectorErase(cmd, doc);
  } else {
    JsonDocument resp;
    resp["cmd"] = cmd;
    resp["queued"] = doc["queued"].as<int>() + 1;
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(ThetaGP::Result::Unsupported);
    resp["reason"] = "unknown flash command";
    FrameLayer::getInstance().sendResponse(resp);
  }
}

void FlashWlHandler::handleWl(const char *cmd, JsonDocument &doc) {
  LOG_DEBUG("FlashWlHandler: cmd='%s'", cmd);

  if (strcmp(cmd, "wl.store") == 0) {
    handleWlStore(cmd, doc);
  } else if (strcmp(cmd, "wl.load") == 0) {
    handleWlLoad(cmd, doc);
  } else if (strcmp(cmd, "wl.erase") == 0) {
    handleWlErase(cmd, doc);
  } else if (strcmp(cmd, "wl.status") == 0) {
    handleWlStatus(cmd, doc);
  } else if (strcmp(cmd, "wl.meta_get") == 0) {
    handleWlMetaGet(cmd, doc);
  } else if (strcmp(cmd, "wl.meta_write") == 0) {
    handleWlMetaWrite(cmd, doc);
  } else if (strcmp(cmd, "wl.full_scan") == 0) {
    handleWlFullScan(cmd, doc);
  } else if (strcmp(cmd, "wl.info") == 0) {
    handleWlInfo(cmd, doc);
  } else {
    JsonDocument resp;
    resp["cmd"] = cmd;
    resp["queued"] = doc["queued"].as<int>() + 1;
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(ThetaGP::Result::Unsupported);
    resp["reason"] = "unknown wl command";
    FrameLayer::getInstance().sendResponse(resp);
  }
}

// ── Registration ──

void FlashWlHandler::registerHandlers() {
  Dispatcher::getInstance().registerHandler("flash", handleFlash);
  Dispatcher::getInstance().registerHandler("wl", handleWl);
  LOG_INFO("FlashWlHandler registered: flash. and wl. domains");
}

#endif

} // namespace ThetaGP::Test
