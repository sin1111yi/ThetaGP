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

#include "test/profile_cmd_handler.h"
#include "test/dispatcher.h"
#include "test/framelayer.h"

#include "gamepad/config/configmgr.h"
#include "gamepad/profile/profile_store.h"
#include "drivers/device/flash/flash_w25qxx.h"

#include "utils/log/log.h"

#include "tusb.h"

#include <cstring>

namespace ThetaGP::Test {

using namespace ThetaGP::Gamepad::Profile;
using ConfigMgr = ThetaGP::Gamepad::Config::ConfigManager;

// Static context for RAW capture callback
static uint16_t s_pendingProfileId = 0;

// Staging buffer for building response JSON
static COMMON_ZERO_INIT char s_profRespBuf[2048];

// ── Helpers ──

static uint8_t hexNibble(char c) {
  if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
  return 0;
}

static uint16_t hexDecode(const char *hex, uint8_t *buf, uint16_t hexLen, uint16_t maxLen) {
  if (!hex || !buf || hexLen == 0) return 0;
  uint16_t byteLen = (hexLen / 2 < maxLen) ? (hexLen / 2) : maxLen;
  for (uint16_t i = 0; i < byteLen; i++) {
    buf[i] = (hexNibble(hex[i * 2]) << 4) | hexNibble(hex[i * 2 + 1]);
  }
  return byteLen;
}

static void sendError(const char *cmd, const Json &json,
                      int errorCode, const char *reason) {
  int q = json.getInt("queued");
  Json resp;
  resp.beginWrite(s_profRespBuf, sizeof(s_profRespBuf));
  resp.printf("{cmd:%Q,queued:%d,status:%Q,error_code:%d,reason:%Q}",
              cmd, q + 1, "error", errorCode, reason);
  uint16_t len = resp.end();
  FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

// ── findProfileAddress ──

static bool findProfileAddress(uint16_t profileId, uint32_t &outAddr,
                               uint16_t &outLen) {
  if (profileId == 0) {
    outAddr = PROFILE0_ADDR;
    auto &flash = Drivers::Device::FlashW25qxx::getInstance();
    flash.read(PROFILE0_ADDR, s_staging, PROFILE_JSON_MAX);
    uint16_t len = 0;
    for (uint16_t i = 0; i < PROFILE_JSON_MAX; i++) {
      if (s_staging[i] == 0 || s_staging[i] == 0xFF) break;
      len = i + 1;
    }
    outLen = len;
    return (len > 0);
  }

  auto &flash = Drivers::Device::FlashW25qxx::getInstance();
  uint16_t bestSeq = 0;
  uint32_t addr = 0;

  for (uint16_t i = 0; i < ADDR_RING_SLOTS; i++) {
    uint32_t slotAddr = ADDR_RING_BASE + i * sizeof(AddressEntry);
    AddressEntry entry;
    if (!flash.read(slotAddr, reinterpret_cast<uint8_t *>(&entry),
                    sizeof(AddressEntry))) {
      continue;
    }
    if (entry.profileId == PROFILE_ID_NONE) {
      continue;
    }
    if (entry.profileId == profileId && entry.seq > bestSeq) {
      bestSeq = entry.seq;
      addr = entry.address;
    }
  }

  if (addr == 0) {
    return false;
  }

  outAddr = addr;

  flash.read(addr, s_staging, PROFILE_JSON_MAX);
  uint16_t len = 0;
  for (uint16_t i = 0; i < PROFILE_JSON_MAX; i++) {
    if (s_staging[i] == 0 || s_staging[i] == 0xFF) break;
    len = i + 1;
  }
  outLen = len;
  return true;
}

// ── onStagingDone ──
// Callback invoked by FrameLayer after RAW capture fills s_staging during
// profile.start. Uses s_pendingProfileId to decide writeFactoryProfile (id=0)
// vs createProfile (id=1-15).

static void onStagingDone(const uint8_t *buf, uint16_t len) {
  const char *json = reinterpret_cast<const char *>(buf);

  Json resp;
  resp.beginWrite(s_profRespBuf, sizeof(s_profRespBuf));

  if (s_pendingProfileId == 0) {
    if (!ProfileStore::getInstance().writeFactoryProfile(json, len)) {
      resp.printf("{cmd:%Q,queued:%d,status:%Q,reason:%Q}",
                  "profile.start", 0, "error", "writeFactoryProfile failed");
      uint16_t rlen = resp.end();
      FrameLayer::getInstance().sendResponse(resp.c_str(), rlen);
      LOG_ERROR("ProfileCmdHandler: writeFactoryProfile failed");
      return;
    }
    resp.printf("{cmd:%Q,queued:%d,status:%Q,profile_id:%d}",
                "profile.start", 0, "ok", 0);
  } else {
    uint16_t newId = 0;
    if (!ProfileStore::getInstance().createProfile(json, len, &newId)) {
      resp.printf("{cmd:%Q,queued:%d,status:%Q,reason:%Q}",
                  "profile.start", 0, "error", "createProfile failed");
      uint16_t rlen = resp.end();
      FrameLayer::getInstance().sendResponse(resp.c_str(), rlen);
      LOG_ERROR("ProfileCmdHandler: createProfile failed");
      return;
    }
    resp.printf("{cmd:%Q,queued:%d,status:%Q,profile_id:%d}",
                "profile.start", 0, "ok", newId);
  }

  uint16_t rlen = resp.end();
  LOG_INFO("ProfileCmdHandler: profile.start done, id=%u", s_pendingProfileId);
  FrameLayer::getInstance().sendResponse(resp.c_str(), rlen);
}

// ── profile.start ──

static void handleProfileStart(const char *cmd, const Json &json) {
  int rawLen = json.getInt("len", 0);
  if (rawLen <= 0 || rawLen > 2048) {
    sendError(cmd, json, 1, "invalid or missing len (1-2048)");
    return;
  }

  int profileId = json.getInt("profileId", 0);
  if (profileId < 0 || profileId > 15) {
    sendError(cmd, json, 1, "profileId out of range (0-15)");
    return;
  }

  s_pendingProfileId = static_cast<uint16_t>(profileId);
  int q = json.getInt("queued");

  Json ack;
  ack.beginWrite(s_profRespBuf, sizeof(s_profRespBuf));
  ack.printf("{cmd:%Q,queued:%d,status:%Q,len:%d,profileId:%d}",
             cmd, q + 1, "ok", rawLen, profileId);
  uint16_t len = ack.end();
  FrameLayer::getInstance().sendResponse(ack.c_str(), len);

  FrameLayer::getInstance().startRawCapture(
      reinterpret_cast<uint8_t *>(ThetaGP::Gamepad::Profile::s_staging),
      static_cast<uint16_t>(rawLen), onStagingDone);
}

// ── profile.get ──

static void handleProfileGet(const char *cmd, const Json &json) {
  int profileId = json.getInt("id", -1);
  if (profileId < 0 || profileId > 15) {
    sendError(cmd, json, 1, "invalid or missing id (0-15)");
    return;
  }

  uint32_t addr = 0;
  uint16_t dataLen = 0;
  if (!findProfileAddress(static_cast<uint16_t>(profileId), addr, dataLen) || dataLen == 0) {
    sendError(cmd, json, 1, "profile not found or empty");
    return;
  }

  // Send header with raw binary length
  char hdrBuf[128];
  int n = snprintf(hdrBuf, sizeof(hdrBuf) - 2,
                   "{\"cmd\":\"profile.start\",\"len\":%u}\r\n", dataLen);
  tud_cdc_write(hdrBuf, static_cast<uint16_t>(n));
  tud_cdc_write_flush();

  // Send raw bytes from staging — tud_task() yields to the USB
  // stack so the host can drain the CDC FIFO between writes
  uint16_t sent = 0;
  while (sent < dataLen) {
    tud_task();
    uint16_t chunk = (dataLen - sent < 64) ? (dataLen - sent) : 64;
    uint32_t written = tud_cdc_write(
        ThetaGP::Gamepad::Profile::s_staging + sent, chunk);
    sent += static_cast<uint16_t>(written);
    if (written < chunk) {
      tud_cdc_write_flush();
      tud_task();
    }
  }
  tud_cdc_write_flush();

  // Send trailer
  Json trailer;
  trailer.beginWrite(s_profRespBuf, sizeof(s_profRespBuf));
  trailer.printf("{cmd:%Q,status:%Q,id:%d}", "profile.end", "ok", profileId);
  uint16_t tlen = trailer.end();
  FrameLayer::getInstance().sendResponse(trailer.c_str(), tlen);
}

// ── profile.list ──

static void handleProfileList(const char *cmd, const Json &json) {
  int q = json.getInt("queued");
  ProfileStatus status = ProfileStore::getInstance().getStatus();

  auto &flash = Drivers::Device::FlashW25qxx::getInstance();

  uint32_t addrMap[16] = {0};
  uint16_t seqMap[16] = {0};

  for (uint16_t i = 0; i < ADDR_RING_SLOTS; i++) {
    uint32_t slotAddr = ADDR_RING_BASE + i * sizeof(AddressEntry);
    AddressEntry entry;
    if (!flash.read(slotAddr, reinterpret_cast<uint8_t *>(&entry),
                    sizeof(AddressEntry))) {
      continue;
    }
    if (entry.profileId == PROFILE_ID_NONE) continue;
    if (entry.profileId > 15) continue;
    if (entry.seq > seqMap[entry.profileId]) {
      seqMap[entry.profileId] = entry.seq;
      addrMap[entry.profileId] = entry.address;
    }
  }

  // Build a comma-separated list of profiles
  char profilesBuf[512];
  int pos = 0;
  pos += snprintf(profilesBuf + pos, sizeof(profilesBuf) - static_cast<size_t>(pos),
                  "[");
  bool first = true;
  for (uint16_t id = 0; id <= 15; id++) {
    if (id == 0 || addrMap[id] != 0) {
      if (!first) {
        pos += snprintf(profilesBuf + pos, sizeof(profilesBuf) - static_cast<size_t>(pos),
                        ",");
      }
      pos += snprintf(profilesBuf + pos, sizeof(profilesBuf) - static_cast<size_t>(pos),
                      "{\"id\":%d,\"active\":%d}", id,
                      (id == status.activeId) ? 1 : 0);
      first = false;
    }
  }
  snprintf(profilesBuf + pos, sizeof(profilesBuf) - static_cast<size_t>(pos), "]");

  Json resp;
  resp.beginWrite(s_profRespBuf, sizeof(s_profRespBuf));
  resp.printf("{cmd:%Q,queued:%d,status:%Q,active:%d,profiles:%s}",
              cmd, q + 1, "ok", status.activeId, profilesBuf);
  uint16_t len = resp.end();
  FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

// ── profile.create ──

static void handleProfileCreate(const char *cmd, const Json &json) {
  int dataHexLen = 0;
  const char *dataHex = json.getStr("data_hex", &dataHexLen);

  if (!dataHex || dataHexLen == 0) {
    sendError(cmd, json, 1, "data_hex field required");
    return;
  }

  uint16_t len = hexDecode(dataHex, s_staging, dataHexLen, PROFILE_JSON_MAX);
  if (len == 0) {
    sendError(cmd, json, 1, "no data decoded from data_hex");
    return;
  }

  if (len < PROFILE_JSON_MAX) {
    s_staging[len] = '\0';
  }

  uint16_t newId = 0;
  if (!ProfileStore::getInstance().createProfile(
          reinterpret_cast<const char *>(s_staging), len, &newId)) {
    sendError(cmd, json, 1, "createProfile failed");
    return;
  }

  int q = json.getInt("queued");
  Json resp;
  resp.beginWrite(s_profRespBuf, sizeof(s_profRespBuf));
  resp.printf("{cmd:%Q,queued:%d,status:%Q,profile_id:%d,len:%d}",
              cmd, q + 1, "ok", newId, len);
  uint16_t rlen = resp.end();
  FrameLayer::getInstance().sendResponse(resp.c_str(), rlen);
}

// ── profile.delete ──

static void handleProfileDelete(const char *cmd, const Json &json) {
  int profileId = json.getInt("id", -1);
  if (profileId <= 0 || profileId > 15) {
    sendError(cmd, json, 1, "invalid id (1-15), cannot delete Profile0");
    return;
  }

  if (!ProfileStore::getInstance().deleteProfile(static_cast<uint16_t>(profileId))) {
    sendError(cmd, json, 1, "deleteProfile failed (profile may not exist)");
    return;
  }

  int q = json.getInt("queued");
  Json resp;
  resp.beginWrite(s_profRespBuf, sizeof(s_profRespBuf));
  resp.printf("{cmd:%Q,queued:%d,status:%Q,id:%d}",
              cmd, q + 1, "ok", profileId);
  uint16_t len = resp.end();
  FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

// ── profile.select ──

static void handleProfileSelect(const char *cmd, const Json &json) {
  int profileId = json.getInt("id", -1);
  if (profileId < 0 || profileId > 15) {
    sendError(cmd, json, 1, "invalid or missing id (0-15)");
    return;
  }

  if (!ProfileStore::getInstance().selectProfile(static_cast<uint16_t>(profileId))) {
    sendError(cmd, json, 1, "selectProfile failed");
    return;
  }

  ConfigMgr::getInstance().setActiveProfileId(static_cast<uint16_t>(profileId));

  uint16_t dataLen = 0;
  ProfileStore::getInstance().loadActive(nullptr, &dataLen);

  int q = json.getInt("queued");
  Json resp;
  resp.beginWrite(s_profRespBuf, sizeof(s_profRespBuf));
  resp.printf("{cmd:%Q,queued:%d,status:%Q,id:%d}",
              cmd, q + 1, "ok", profileId);
  uint16_t len = resp.end();
  FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

// ── profile.status ──

static void handleProfileStatus(const char *cmd, const Json &json) {
  ProfileStatus status = ProfileStore::getInstance().getStatus();
  int q = json.getInt("queued");

  Json resp;
  resp.beginWrite(s_profRespBuf, sizeof(s_profRespBuf));
  resp.printf("{cmd:%Q,queued:%d,status:%Q,active_profile_id:%d,"
              "profile_count:%d,next_addr:%lu,boot_meta_seq:%d,"
              "address_ring_seq:%d,total_sectors:%d,used_sectors:%d,"
              "free_sectors:%d}",
              cmd, q + 1, "ok",
              status.activeId, status.profileCount,
              (unsigned long)status.nextAddr,
              status.bootMetaSeq, status.addressRingSeq,
              status.totalSectors, status.usedSectors,
              status.freeSectors);
  uint16_t len = resp.end();
  FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

// ── profile.save ──
// Delegate to ConfigManager::saveProfile()

static void handleProfileSave(const char *cmd, const Json &json) {
  if (!ConfigMgr::getInstance().saveProfile()) {
    sendError(cmd, json, 1, "saveProfile failed");
    return;
  }

  int q = json.getInt("queued");
  Json resp;
  resp.beginWrite(s_profRespBuf, sizeof(s_profRespBuf));
  resp.printf("{cmd:%Q,queued:%d,status:%Q,id:%d}",
              cmd, q + 1, "ok",
              ConfigMgr::getInstance().activeProfileId());
  uint16_t len = resp.end();
  FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

// ── profile.load ──
// Delegate to ConfigManager::loadProfile()

static void handleProfileLoad(const char *cmd, const Json &json) {
  int profileId = json.getInt("id", -1);

  ProfileStatus status = ProfileStore::getInstance().getStatus();

  if (profileId > 15) {
    profileId = status.activeId;
  }

  if (!ConfigMgr::getInstance().loadProfile(static_cast<uint16_t>(profileId))) {
    sendError(cmd, json, 1, "loadProfile failed");
    return;
  }

  int q = json.getInt("queued");
  Json resp;
  resp.beginWrite(s_profRespBuf, sizeof(s_profRespBuf));
  resp.printf("{cmd:%Q,queued:%d,status:%Q,id:%d}",
              cmd, q + 1, "ok", profileId);
  uint16_t len = resp.end();
  FrameLayer::getInstance().sendResponse(resp.c_str(), len);
}

// ── Domain Dispatch ──

void ProfileCmdHandler::handleProfile(const char *cmd, const Json &json) {
  LOG_DEBUG("ProfileCmdHandler: cmd='%s'", cmd);

  if (strcmp(cmd, "profile.start") == 0) {
    handleProfileStart(cmd, json);
  } else if (strcmp(cmd, "profile.get") == 0) {
    handleProfileGet(cmd, json);
  } else if (strcmp(cmd, "profile.list") == 0) {
    handleProfileList(cmd, json);
  } else if (strcmp(cmd, "profile.create") == 0) {
    handleProfileCreate(cmd, json);
  } else if (strcmp(cmd, "profile.delete") == 0) {
    handleProfileDelete(cmd, json);
  } else if (strcmp(cmd, "profile.select") == 0) {
    handleProfileSelect(cmd, json);
  } else if (strcmp(cmd, "profile.status") == 0) {
    handleProfileStatus(cmd, json);
  } else if (strcmp(cmd, "profile.save") == 0) {
    handleProfileSave(cmd, json);
  } else if (strcmp(cmd, "profile.load") == 0) {
    handleProfileLoad(cmd, json);
  } else {
    int q = json.getInt("queued");
    Json resp;
    resp.beginWrite(s_profRespBuf, sizeof(s_profRespBuf));
    resp.printf("{cmd:%Q,queued:%d,status:%Q,error_code:%d,reason:%Q}",
                cmd, q + 1, "error",
                static_cast<int>(ThetaGP::Result::Unsupported),
                "unknown profile command");
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
  }
}

// ── Registration ──

ProfileCmdHandler &ProfileCmdHandler::getInstance() {
  static ProfileCmdHandler instance;
  return instance;
}

void ProfileCmdHandler::registerHandlers() {
  Dispatcher::getInstance().registerHandler("profile", handleProfile);
  LOG_INFO("ProfileCmdHandler registered: profile. domain");
}

} // namespace ThetaGP::Test
